#ifndef HTML_UI_PACKAGE_H
#define HTML_UI_PACKAGE_H

/*
 * html_ui_package.h
 *
 * HTML-to-UI package interface for BT817 screens.
 * This layer consumes html_lite parser events, renders supported semantic tags,
 * and builds/dispatches input actions (key/touch) for HTML-driven screens.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HTML_UI_MAX_ACTIONS 24
#define HTML_UI_MAX_KEYS_PER_ACTION 8
#define HTML_UI_MAX_TARGET_FILE_LEN 64
#define HTML_UI_TEMPLATE_PATH_MAX 96

typedef uintptr_t html_ui_color_t;

typedef enum {
    HTML_UI_SCREEN_STARTUP_HOME = 0,
    HTML_UI_SCREEN_DUMMY_LANGUAGE,
    HTML_UI_SCREEN_DUMMY_MODE,
    HTML_UI_SCREEN_DUMMY_SCAN,
    HTML_UI_SCREEN_DUMMY_COURIER,
    _HTML_UI_SCREEN_COUNT
} html_ui_screen_t;

typedef struct {
    uint8_t tag;
    char id[32];
    char action[24];
    bool has_target_html_screen;
    html_ui_screen_t target_html_screen;
    bool has_target_template_file;
    char target_template_file[HTML_UI_MAX_TARGET_FILE_LEN];
    char keys[HTML_UI_MAX_KEYS_PER_ACTION + 1];
    char key;
} html_ui_action_t;

typedef struct {
    int button_width;
    int button_height;
    int button_font_size;
    int text_center_x_option;
    html_ui_color_t background_color;
    html_ui_color_t primary_color;
    html_ui_color_t error_color;
    html_ui_color_t disable_color;
    html_ui_color_t text_color;
    html_ui_color_t disable_text_color;
    html_ui_color_t available_compartment_color;
    html_ui_color_t input_line_color;
    html_ui_color_t summary_line_color;
    html_ui_color_t information_line_color;
    html_ui_color_t information_box_color;
    html_ui_color_t circle_text_color;
    html_ui_color_t header_box_color;
    html_ui_color_t test_red_color;
    html_ui_color_t test_blue_color;
    html_ui_color_t test_white_color;
    html_ui_color_t test_green_color;
    html_ui_color_t test_black_color;
} html_ui_style_t;

typedef struct {
    bool (*get_display_size)(void *render_ctx, int *width, int *height);
    bool (*is_dark_mode_enabled)(void *platform_user);
    bool (*localization_exists)(void *platform_user, const char *page, const char *key);
    void (*fill_rect)(void *render_ctx,
                      int x1,
                      int y1,
                      int x2,
                      int y2,
                      html_ui_color_t color);
    void (*draw_line)(void *render_ctx,
                      int x1,
                      int y1,
                      int x2,
                      int y2,
                      int line_width,
                      html_ui_color_t color);
    void (*draw_text)(void *render_ctx,
                      int x,
                      int y,
                      int font,
                      int options,
                      html_ui_color_t color,
                      const char *page,
                      const char *key);
    void (*draw_icon)(void *render_ctx, const char *icon_name, int x, int y, bool center);
    void (*set_touch_tag)(void *render_ctx, uint8_t tag);
    uint8_t (*read_touch_tag)(void *render_ctx);
} html_ui_platform_t;

typedef bool (*html_ui_action_handler_t)(const html_ui_action_t *action, void *user);

typedef struct {
    void *render_ctx;
    void *platform_user;
    const html_ui_platform_t *platform;
    html_ui_style_t style;
    uint8_t next_auto_tag;
    html_ui_screen_t current_screen;
    html_ui_action_t actions[HTML_UI_MAX_ACTIONS];
    size_t action_count;
} html_ui_context_t;

/* Initialize HTML UI context and configure first auto-generated touch tag. */
void html_ui_init(html_ui_context_t *ctx, void *phost, uint8_t first_auto_tag);

/* Initialize context with an explicit platform adapter and style overrides. */
void html_ui_init_with_platform(html_ui_context_t *ctx,
                                void *render_ctx,
                                const html_ui_platform_t *platform,
                                void *platform_user,
                                const html_ui_style_t *style,
                                uint8_t first_auto_tag);

/* Apply a style override after initialization. */
void html_ui_apply_style(html_ui_context_t *ctx, const html_ui_style_t *style);

/* Built-in Swipbox adapter and defaults used by html_ui_init(). */
const html_ui_platform_t *html_ui_default_platform(void);
const html_ui_style_t *html_ui_default_style(void);

/* Reset action table to empty state without changing selected screen. */
void html_ui_reset_actions(html_ui_context_t *ctx);

/* Set current HTML screen id used by html_ui_render template selection logic. */
void html_ui_set_screen(html_ui_context_t *ctx, html_ui_screen_t screen);

/* Read current HTML screen id from context. */
html_ui_screen_t html_ui_get_screen(const html_ui_context_t *ctx);

/*
 * Render HTML into BT817 commands and build an in-memory action map.
 */
void html_ui_render(html_ui_context_t *ctx, const char *html);

/*
 * Resolve touch/key input against the action map.
 * If no custom handler is supplied, default behavior is:
 * - when has_target_html_screen=true, switch html_ui_context_t.current_screen
 */
bool html_ui_handle_input(html_ui_context_t *ctx, char key, html_ui_action_handler_t handler, void *user);

#ifdef __cplusplus
}
#endif

#endif
