# Integrating `htm_package_oled` Into a New Project

This package is a copy-ready OLED variant of the HTML UI renderer. It removes BT817 dependencies and uses Zephyr `display` APIs.

## 1. Copy The Folder

Copy the whole folder below into your new project:

- `src/htm_package_oled/`

Do not copy files one by one. Keep all files together.

## 2. Required Files In This Package

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
- `html_ui_platform_swipbox.c` (OLED Zephyr backend in this package)
- `HTML_PARSING_README.md`
- `HTML_RENDERER_PORTING_GUIDE.md`
- `INTEGRATION_GUIDE.md`

## 3. Enable Kconfig Options

In your target project's `prj.conf` add:

```ini
CONFIG_DISPLAY=y
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=3

CONFIG_FILE_SYSTEM=y
CONFIG_FILE_SYSTEM_LITTLEFS=y
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_NVS=y
```

For your OLED controller add the proper driver option, for example one of:

```ini
CONFIG_SSD1306=y
# CONFIG_SSD1322=y
# CONFIG_SSD1333=y
# CONFIG_SSD1351=y
```

## 4. Add Board Overlay For OLED

In your board `.overlay` add your OLED bus node (`I2C` or `SPI`) and pins.

Example shape:

```dts
&i2c1 {
    status = "okay";

    oled@3c {
        compatible = "solomon,ssd1306fb";
        reg = <0x3c>;
        width = <128>;
        height = <64>;
        segment-offset = <0>;
        page-offset = <0>;
        display-offset = <0>;
        multiplex-ratio = <63>;
        prechargep = <0x22>;
    };
};
```

## 5. Add Sources To Build

In `CMakeLists.txt` of the target app, add all package `.c` files:

```cmake
target_sources(app PRIVATE
  src/htm_package_oled/html_lite.c
  src/htm_package_oled/html_ui_package.c
  src/htm_package_oled/html_ui_templates.c
  src/htm_package_oled/html_ui_template_store.c
  src/htm_package_oled/html_ui_platform_swipbox.c
)
```

## 6. Basic Runtime Init

```c
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include "htm_package_oled/html_ui_package.h"

static html_ui_context_t g_html_ctx;

void ui_init(void)
{
    const struct device *display = DEVICE_DT_GET_ANY(solomon_ssd1306fb);

    if (!device_is_ready(display)) {
        return;
    }

    html_ui_init(&g_html_ctx, (void *)display, 10);
    html_ui_set_screen(&g_html_ctx, HTML_UI_SCREEN_STARTUP_HOME);
}

void ui_render_frame(const char *html)
{
    html_ui_render(&g_html_ctx, html);
}

void ui_handle_input(char key)
{
    (void)html_ui_handle_input(&g_html_ctx, key, NULL, NULL);
}
```

## 7. Notes

- Default backend expects a Zephyr display device pointer as `render_ctx`.
- Touch tags are not wired by default in OLED backend. Key input still works.
- Color defaults are RGB565-style tokens; monochrome OLEDs are still supported with reduced visual fidelity.
- Adjust templates for small OLED resolutions (for example 128x64 or 128x128).
