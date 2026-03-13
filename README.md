# OLED HTML Demo Project

Zephyr firmware demo that renders HTML-driven UI templates on an OLED display.

## Project Layout

- `src/main.c`: App entry point and render loop.
- `src/htm_package_oled/`: HTML parser, UI schema, templates, and OLED platform renderer.
- `app.overlay`: Board OLED device-tree overlay.
- `prj.conf`: Zephyr Kconfig settings for display/filesystem/logging.

## Prerequisites

- Nordic nRF Connect SDK or Zephyr SDK toolchain installed.
- `west` initialized and available on your `PATH`.
- A supported board target (this repo is currently configured for `bl5340_dvk/nrf5340/cpuapp`).

## Build

From this repository root:

```bash
west build -b bl5340_dvk/nrf5340/cpuapp -p auto
```

For another board:

```bash
west build -b <your_board> -p auto
```

## Flash

```bash
west flash
```

If you have multiple probes connected, pass runner arguments as needed for your setup.

## Run Behavior

At boot, the app:

- Initializes the OLED display device.
- Initializes HTML UI context.
- Sets startup screen (`HTML_UI_SCREEN_STARTUP_HOME`).
- Renders a built-in template (`HTML_UI_TEMPLATE_SCAN_HOME`).
- Polls input in a 50 ms loop.

## Required Kconfig

`prj.conf` already enables:

- `CONFIG_DISPLAY=y`
- `CONFIG_LOG=y`
- `CONFIG_FILE_SYSTEM=y`
- `CONFIG_FILE_SYSTEM_LITTLEFS=y`
- `CONFIG_FLASH=y`
- `CONFIG_FLASH_MAP=y`
- `CONFIG_NVS=y`

Add your OLED driver option if needed (example: `CONFIG_SSD1306=y`).

## Board Overlay

Update `app.overlay` with your board-specific OLED node (I2C/SPI bus, pins, and compatible).
The provided overlay is a starting point and may require pin/bus changes.

## Troubleshooting

- `west: command not found`: activate your Zephyr/NCS environment first.
- Display include errors in editor (`CONFIG_DISPLAY`): run a clean configure/build so compile commands regenerate.
- No OLED output: verify overlay wiring, driver Kconfig, and that the display node is `okay`.
