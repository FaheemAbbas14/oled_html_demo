#ifndef HTML_UI_TEMPLATE_STORE_H
#define HTML_UI_TEMPLATE_STORE_H

/*
 * html_ui_template_store.h
 *
 * Zephyr/LittleFS-backed HTML template loading and fallback rendering.
 * This module is intentionally separate from the core HTML renderer so the
 * renderer can be reused without carrying filesystem dependencies.
 */

#include <stdbool.h>
#include <stddef.h>

#include "html_ui_package.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Load an HTML template from LittleFS without creating the file.
 * Returns the number of bytes read, or a negative error code.
 */
int html_ui_load_template_from_lfs(const char *template_path, char *buffer, size_t buffer_size);

/*
 * Render from a LittleFS template file when present, otherwise use fallback_html.
 * Returns true when the LittleFS file was used, false when fallback_html was used.
 */
bool html_ui_render_template_from_lfs_or_fallback(html_ui_context_t *ctx,
                                                  const char *template_path,
                                                  const char *fallback_html,
                                                  char *buffer,
                                                  size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif