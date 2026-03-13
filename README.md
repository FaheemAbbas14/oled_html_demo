# OLED HTML Demo Project

This is a new Zephyr demo project that already includes the copy-ready OLED HTML package.

## Included Package

- `src/htm_package_oled/`

## Build

```bash
west build -b <your_board> oled_html_demo
```

Example:

```bash
west build -b nrf5340dk/nrf5340/cpuapp oled_html_demo
```

## Required Kconfig

`prj.conf` already enables:

- `CONFIG_DISPLAY=y`
- `CONFIG_FILE_SYSTEM=y`
- `CONFIG_FILE_SYSTEM_LITTLEFS=y`

Add your OLED driver option if needed (for example `CONFIG_SSD1306=y`).

## Board Overlay

Add your board-specific OLED node in `app.overlay`.
A starter `app.overlay` is provided and may need pin/bus updates for your board.
