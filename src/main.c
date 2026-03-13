#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "htm_package_oled/html_ui_package.h"
#include "htm_package_oled/html_ui_templates.h"

LOG_MODULE_REGISTER(oled_html_demo, LOG_LEVEL_INF);

static html_ui_context_t g_html_ctx;

int main(void)
{
    const struct device *display = DEVICE_DT_GET_ANY(solomon_ssd1306fb);

    if (display == NULL || !device_is_ready(display)) {
        LOG_ERR("OLED display device is not ready");
        return 0;
    }

    html_ui_init(&g_html_ctx, (void *)display, 10);
    html_ui_set_screen(&g_html_ctx, HTML_UI_SCREEN_STARTUP_HOME);

    /* Render a built-in template to verify package integration. */
    html_ui_render(&g_html_ctx, HTML_UI_TEMPLATE_SCAN_HOME);

    while (1) {
        (void)html_ui_handle_input(&g_html_ctx, 0, NULL, NULL);
        k_sleep(K_MSEC(50));
    }

    return 0;
}
