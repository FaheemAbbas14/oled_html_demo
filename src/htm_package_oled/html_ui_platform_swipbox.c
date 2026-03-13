#include "html_ui_package.h"

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(html_ui_oled, LOG_LEVEL_INF);

#define HTML_UI_OLED_MONO_BLACK 0u
#define HTML_UI_OLED_MONO_WHITE 1u
#define HTML_UI_OLED_RGB565_BLACK 0x0000u
#define HTML_UI_OLED_RGB565_WHITE 0xFFFFu
#define HTML_UI_OLED_RGB565_RED 0xF800u
#define HTML_UI_OLED_RGB565_BLUE 0x001Fu
#define HTML_UI_OLED_RGB565_GREEN 0x07E0u
#define HTML_UI_OLED_RGB565_GRAY 0x8410u
#define HTML_UI_OLED_RGB565_YELLOW 0xFFE0u
#define HTML_UI_OLED_RGB565_CYAN 0x07FFu
#define HTML_UI_OLED_ROW_PIXELS_MAX 256u

typedef struct {
    const struct device *display;
    struct display_capabilities caps;
    bool initialized;
    uint8_t last_touch_tag;
} html_ui_oled_runtime_t;

static html_ui_oled_runtime_t g_oled_runtime;

static bool oled_ensure_init(void *render_ctx)
{
    const struct device *display = (const struct device *)render_ctx;

    if (display == NULL || !device_is_ready(display)) {
        return false;
    }

    if (g_oled_runtime.initialized && g_oled_runtime.display == display) {
        return true;
    }

    memset(&g_oled_runtime, 0, sizeof(g_oled_runtime));
    g_oled_runtime.display = display;
    display_get_capabilities(display, &g_oled_runtime.caps);

    if ((g_oled_runtime.caps.supported_pixel_formats & PIXEL_FORMAT_RGB_565) != 0u) {
        (void)display_set_pixel_format(display, PIXEL_FORMAT_RGB_565);
    } else if ((g_oled_runtime.caps.supported_pixel_formats & PIXEL_FORMAT_MONO01) != 0u) {
        (void)display_set_pixel_format(display, PIXEL_FORMAT_MONO01);
    } else if ((g_oled_runtime.caps.supported_pixel_formats & PIXEL_FORMAT_MONO10) != 0u) {
        (void)display_set_pixel_format(display, PIXEL_FORMAT_MONO10);
    } else {
        LOG_ERR("Unsupported OLED pixel format mask: 0x%x", g_oled_runtime.caps.supported_pixel_formats);
        return false;
    }

    (void)display_blanking_off(display);
    g_oled_runtime.initialized = true;
    return true;
}

static bool oled_is_rgb565(void)
{
    return g_oled_runtime.caps.current_pixel_format == PIXEL_FORMAT_RGB_565;
}

static uint16_t oled_color_to_rgb565(html_ui_color_t color)
{
    return (uint16_t)(color & 0xFFFFu);
}

static uint8_t oled_color_to_mono(html_ui_color_t color)
{
    return (color == (html_ui_color_t)HTML_UI_OLED_MONO_BLACK) ? 0u : 1u;
}

static bool swipbox_get_display_size(void *render_ctx, int *width, int *height)
{
    if (width == NULL || height == NULL || !oled_ensure_init(render_ctx)) {
        return false;
    }

    *width = (int)g_oled_runtime.caps.x_resolution;
    *height = (int)g_oled_runtime.caps.y_resolution;
    return (*width > 0 && *height > 0);
}

static bool swipbox_is_dark_mode_enabled(void *platform_user)
{
    ARG_UNUSED(platform_user);
    return false;
}

static bool swipbox_localization_exists(void *platform_user, const char *page, const char *key)
{
    ARG_UNUSED(platform_user);
    ARG_UNUSED(page);
    return (key != NULL && *key != '\0');
}

static void oled_write_row_rgb565(const struct device *display,
                                  int x,
                                  int y,
                                  int w,
                                  uint16_t color)
{
    uint16_t row[HTML_UI_OLED_ROW_PIXELS_MAX];
    struct display_buffer_descriptor desc;
    int i;

    if (w <= 0 || w > (int)HTML_UI_OLED_ROW_PIXELS_MAX) {
        return;
    }

    for (i = 0; i < w; i++) {
        row[i] = color;
    }

    memset(&desc, 0, sizeof(desc));
    desc.width = (uint16_t)w;
    desc.height = 1u;
    desc.pitch = (uint16_t)w;
    desc.buf_size = (size_t)w * sizeof(uint16_t);
    (void)display_write(display, x, y, &desc, row);
}

static void oled_write_pixel_rgb565(const struct device *display, int x, int y, uint16_t color)
{
    struct display_buffer_descriptor desc = {
        .buf_size = sizeof(color),
        .width = 1u,
        .height = 1u,
        .pitch = 1u,
    };

    (void)display_write(display, x, y, &desc, &color);
}

static void oled_write_pixel_mono(const struct device *display, int x, int y, uint8_t color)
{
    uint8_t pixel = color ? 0x01u : 0x00u;
    struct display_buffer_descriptor desc = {
        .buf_size = sizeof(pixel),
        .width = 1u,
        .height = 1u,
        .pitch = 1u,
    };

    (void)display_write(display, x, y, &desc, &pixel);
}

static void swipbox_fill_rect(void *render_ctx,
                              int x1,
                              int y1,
                              int x2,
                              int y2,
                              html_ui_color_t color)
{
    const struct device *display;
    int x_start;
    int y_start;
    int x_end;
    int y_end;
    int x;
    int y;

    if (!oled_ensure_init(render_ctx)) {
        return;
    }

    display = g_oled_runtime.display;
    x_start = MIN(x1, x2);
    y_start = MIN(y1, y2);
    x_end = MAX(x1, x2);
    y_end = MAX(y1, y2);

    if (x_end < 0 || y_end < 0 || x_start >= (int)g_oled_runtime.caps.x_resolution ||
        y_start >= (int)g_oled_runtime.caps.y_resolution) {
        return;
    }

    x_start = CLAMP(x_start, 0, (int)g_oled_runtime.caps.x_resolution - 1);
    y_start = CLAMP(y_start, 0, (int)g_oled_runtime.caps.y_resolution - 1);
    x_end = CLAMP(x_end, 0, (int)g_oled_runtime.caps.x_resolution - 1);
    y_end = CLAMP(y_end, 0, (int)g_oled_runtime.caps.y_resolution - 1);

    if (oled_is_rgb565()) {
        uint16_t rgb565 = oled_color_to_rgb565(color);
        int width = x_end - x_start + 1;

        if (width <= (int)HTML_UI_OLED_ROW_PIXELS_MAX) {
            for (y = y_start; y <= y_end; y++) {
                oled_write_row_rgb565(display, x_start, y, width, rgb565);
            }
        } else {
            for (y = y_start; y <= y_end; y++) {
                for (x = x_start; x <= x_end; x++) {
                    oled_write_pixel_rgb565(display, x, y, rgb565);
                }
            }
        }
    } else {
        uint8_t mono = oled_color_to_mono(color);
        for (y = y_start; y <= y_end; y++) {
            for (x = x_start; x <= x_end; x++) {
                oled_write_pixel_mono(display, x, y, mono);
            }
        }
    }
}

static void swipbox_draw_line(void *render_ctx,
                              int x1,
                              int y1,
                              int x2,
                              int y2,
                              int line_width,
                              html_ui_color_t color)
{
    int dx;
    int dy;
    int sx;
    int sy;
    int err;
    int e2;
    int x = x1;
    int y = y1;

    if (!oled_ensure_init(render_ctx)) {
        return;
    }

    dx = (x2 >= x1) ? (x2 - x1) : (x1 - x2);
    dy = -((y2 >= y1) ? (y2 - y1) : (y1 - y2));
    sx = (x1 < x2) ? 1 : -1;
    sy = (y1 < y2) ? 1 : -1;
    err = dx + dy;

    while (true) {
        int half = MAX(1, line_width) / 2;
        swipbox_fill_rect(render_ctx, x - half, y - half, x + half, y + half, color);

        if (x == x2 && y == y2) {
            break;
        }

        e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y += sy;
        }
    }
}

static void oled_draw_char(void *render_ctx, int x, int y, char c, html_ui_color_t color)
{
    int i;

    if (c == ' ') {
        return;
    }

    /* Minimal fallback font: solid 4x6 glyph box keeps text readable without BT817 font engine. */
    for (i = 0; i < 4; i++) {
        swipbox_draw_line(render_ctx, x + i, y, x + i, y + 5, 1, color);
    }
}

static void swipbox_draw_text(void *render_ctx,
                              int x,
                              int y,
                              int font,
                              int options,
                              html_ui_color_t color,
                              const char *page,
                              const char *key)
{
    const char *text = (key != NULL) ? key : "";
    int i;

    ARG_UNUSED(font);
    ARG_UNUSED(options);
    ARG_UNUSED(page);

    if (!oled_ensure_init(render_ctx)) {
        return;
    }

    for (i = 0; text[i] != '\0'; i++) {
        oled_draw_char(render_ctx, x + (i * 6), y, text[i], color);
    }
}

static void swipbox_draw_icon(void *render_ctx, const char *icon_name, int x, int y, bool center)
{
    int x1 = x;
    int y1 = y;

    ARG_UNUSED(icon_name);

    if (center) {
        x1 -= 8;
        y1 -= 8;
    }

    swipbox_fill_rect(render_ctx, x1, y1, x1 + 15, y1 + 15, (html_ui_color_t)HTML_UI_OLED_RGB565_WHITE);
    swipbox_fill_rect(render_ctx, x1 + 2, y1 + 2, x1 + 13, y1 + 13, (html_ui_color_t)HTML_UI_OLED_RGB565_BLACK);
}

static void swipbox_set_touch_tag(void *render_ctx, uint8_t tag)
{
    ARG_UNUSED(render_ctx);
    g_oled_runtime.last_touch_tag = tag;
}

static uint8_t swipbox_read_touch_tag(void *render_ctx)
{
    ARG_UNUSED(render_ctx);
    return 0u;
}

static const html_ui_platform_t g_html_ui_default_platform = {
    .get_display_size = swipbox_get_display_size,
    .is_dark_mode_enabled = swipbox_is_dark_mode_enabled,
    .localization_exists = swipbox_localization_exists,
    .fill_rect = swipbox_fill_rect,
    .draw_line = swipbox_draw_line,
    .draw_text = swipbox_draw_text,
    .draw_icon = swipbox_draw_icon,
    .set_touch_tag = swipbox_set_touch_tag,
    .read_touch_tag = swipbox_read_touch_tag,
};

static const html_ui_style_t g_html_ui_default_style = {
    .button_width = 96,
    .button_height = 28,
    .button_font_size = 22,
    .text_center_x_option = 0,
    .background_color = (html_ui_color_t)HTML_UI_OLED_RGB565_BLACK,
    .primary_color = (html_ui_color_t)HTML_UI_OLED_RGB565_WHITE,
    .error_color = (html_ui_color_t)HTML_UI_OLED_RGB565_RED,
    .disable_color = (html_ui_color_t)HTML_UI_OLED_RGB565_GRAY,
    .text_color = (html_ui_color_t)HTML_UI_OLED_RGB565_WHITE,
    .disable_text_color = (html_ui_color_t)HTML_UI_OLED_RGB565_GRAY,
    .available_compartment_color = (html_ui_color_t)HTML_UI_OLED_RGB565_GREEN,
    .input_line_color = (html_ui_color_t)HTML_UI_OLED_RGB565_WHITE,
    .summary_line_color = (html_ui_color_t)HTML_UI_OLED_RGB565_CYAN,
    .information_line_color = (html_ui_color_t)HTML_UI_OLED_RGB565_BLUE,
    .information_box_color = (html_ui_color_t)HTML_UI_OLED_RGB565_BLACK,
    .circle_text_color = (html_ui_color_t)HTML_UI_OLED_RGB565_WHITE,
    .header_box_color = (html_ui_color_t)HTML_UI_OLED_RGB565_BLUE,
    .test_red_color = (html_ui_color_t)HTML_UI_OLED_RGB565_RED,
    .test_blue_color = (html_ui_color_t)HTML_UI_OLED_RGB565_BLUE,
    .test_white_color = (html_ui_color_t)HTML_UI_OLED_RGB565_WHITE,
    .test_green_color = (html_ui_color_t)HTML_UI_OLED_RGB565_GREEN,
    .test_black_color = (html_ui_color_t)HTML_UI_OLED_RGB565_BLACK,
};

const html_ui_platform_t *html_ui_default_platform(void)
{
    return &g_html_ui_default_platform;
}

const html_ui_style_t *html_ui_default_style(void)
{
    return &g_html_ui_default_style;
}