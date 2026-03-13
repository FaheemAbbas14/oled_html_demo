#ifndef HTML_LITE_H
#define HTML_LITE_H

/*
 * html_lite.h
 *
 * Lightweight HTML tokenizer/event parser interface.
 * This layer is renderer-agnostic: it only emits parse events
 * (start/end/self-closing/text) and does not perform any UI drawing.
 */

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    HTML_EVT_START_TAG = 0,
    HTML_EVT_END_TAG,
    HTML_EVT_SELF_CLOSING_TAG,
    HTML_EVT_TEXT,
} html_evt_type_t;

typedef struct {
    const char *name;
    const char *value;
} html_attr_t;

typedef struct {
    html_evt_type_t type;
    const char *tag;
    const char *text;
    html_attr_t attrs[16];
    size_t attr_count;
} html_evt_t;

typedef void (*html_evt_cb_t)(const html_evt_t *evt, void *user);

/* Parse input HTML and emit callback events for tags and normalized text nodes. */
void html_parse_lite(const char *html, html_evt_cb_t cb, void *user);

#endif
