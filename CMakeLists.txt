cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(oled_html_demo)

target_sources(app PRIVATE
  src/main.c
  src/htm_package_oled/html_lite.c
  src/htm_package_oled/html_ui_package.c
  src/htm_package_oled/html_ui_platform_swipbox.c
  src/htm_package_oled/html_ui_template_store.c
  src/htm_package_oled/html_ui_templates.c
)

target_include_directories(app PRIVATE src)
