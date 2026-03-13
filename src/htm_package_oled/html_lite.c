#include "html_lite.h"

/*
 * html_lite.c
 *
 * Implementation of the lightweight HTML parser used by the firmware.
 * Responsibility: tokenize HTML and emit generic parse events only.
 * Non-responsibility: UI semantics, drawing, action dispatch, or screen logic.
 * Those are handled in html_ui_package.c.
 */

#include <ctype.h>
#include <stdint.h>
#include <string.h>

#include "html_lite_schema.h"

/*
 * NOTE:
 * Keep src/htm_package/HTML_PARSING_README.md updated when parser behavior
 * changes in this file (event emission, skipped tags, limits, etc.).
 */

static const char *cpy(char *dst, size_t dst_sz, const char *src, size_t n)
{
    if (dst_sz == 0) {
        return "";
    }

    if (n >= dst_sz) {
        n = dst_sz - 1;
    }

    memcpy(dst, src, n);
    dst[n] = 0;
    return dst;
}

/* Advance pointer past any ASCII whitespace characters. */
static void skip_ws(const char **p)
{
    while (**p && isspace((unsigned char)**p)) {
        (*p)++;
    }
}

/* Case-insensitive prefix match used for markup markers and close tags. */
static bool startswith_ci(const char *p, const char *kw)
{
    while (*kw) {
        if (*p == 0) {
            return false;
        }

        if (tolower((unsigned char)*p) != tolower((unsigned char)*kw)) {
            return false;
        }

        p++;
        kw++;
    }

    return true;
}

/* Allowed characters for tag/attribute names in this lightweight parser. */
static bool is_name_char(char c)
{
    return isalnum((unsigned char)c) || c == '-' || c == '_' || c == ':';
}

/* Case-insensitive full string equality helper. */
static bool streq_ci(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        a++;
        b++;
    }

    return *a == 0 && *b == 0;
}

/* Identify HTML void tags that should also emit synthetic self-closing events. */
static bool is_void_tag(const char *tag)
{
    return streq_ci(tag, HTML_LITE_TAG_BR) ||
           streq_ci(tag, HTML_LITE_TAG_IMG) ||
           streq_ci(tag, HTML_LITE_TAG_HR) ||
           streq_ci(tag, HTML_LITE_TAG_META) ||
           streq_ci(tag, HTML_LITE_TAG_LINK) ||
           streq_ci(tag, HTML_LITE_TAG_INPUT);
}

/* Collapse repeated/internal whitespace in text nodes into single spaces. */
static void collapse_ws(char *s)
{
    char *r = s;
    bool in_ws = false;

    while (*s && isspace((unsigned char)*s)) {
        s++;
    }

    for (; *s; s++) {
        if (isspace((unsigned char)*s)) {
            in_ws = true;
            continue;
        }
        if (in_ws && r != s) {
            *r++ = ' ';
        }
        in_ws = false;
        *r++ = *s;
    }

    *r = 0;
}

/* Emit normalized text event when non-empty content exists between tags. */
static void emit_text(html_evt_cb_t cb, void *user, const char *start, size_t len)
{
    static char txt[256];
    html_evt_t evt;

    if (len == 0) {
        return;
    }

    cpy(txt, sizeof(txt), start, len);
    collapse_ws(txt);
    if (txt[0] == 0) {
        return;
    }

    memset(&evt, 0, sizeof(evt));
    evt.type = HTML_EVT_TEXT;
    evt.text = txt;
    cb(&evt, user);
}

/* Parse tag attributes into evt->attrs using fixed scratch storage. */
static void parse_attrs(const char **p, html_evt_t *evt, char *scratch, size_t scratch_sz)
{
    size_t used = 0;

    evt->attr_count = 0;

    while (**p) {
        const char *n0;
        size_t nlen;
        const char *v0 = "";
        size_t vlen = 0;
        const char *name;
        const char *val;

        skip_ws(p);
        if (**p == '>' || (**p == '/' && (*p)[1] == '>')) {
            break;
        }

        /* Hard cap attrs per tag to keep parser memory bounded. */
        if (evt->attr_count >= (sizeof(evt->attrs) / sizeof(evt->attrs[0]))) {
            while (**p && **p != '>' && !(**p == '/' && (*p)[1] == '>')) {
                (*p)++;
            }
            break;
        }

        n0 = *p;
        while (**p && is_name_char(**p)) {
            (*p)++;
        }
        nlen = (size_t)(*p - n0);
        if (nlen == 0) {
            if (**p) {
                (*p)++;
            }
            continue;
        }

        skip_ws(p);

        if (**p == '=') {
            (*p)++;
            skip_ws(p);

            if (**p == '"' || **p == '\'') {
                char q = **p;
                (*p)++;
                v0 = *p;
                while (**p && **p != q) {
                    (*p)++;
                }
                vlen = (size_t)(*p - v0);
                if (**p == q) {
                    (*p)++;
                }
            } else {
                v0 = *p;
                while (**p && !isspace((unsigned char)**p) && **p != '>' && !(**p == '/' && (*p)[1] == '>')) {
                    (*p)++;
                }
                vlen = (size_t)(*p - v0);
            }
        }

        if (used + nlen + 1 >= scratch_sz) {
            return;
        }
        name = cpy(scratch + used, scratch_sz - used, n0, nlen);
        used += nlen + 1;

        if (used + vlen + 1 >= scratch_sz) {
            return;
        }
        val = cpy(scratch + used, scratch_sz - used, v0, vlen);
        used += vlen + 1;

        evt->attrs[evt->attr_count].name = name;
        evt->attrs[evt->attr_count].value = val;
        evt->attr_count++;
    }
}

/*
 * Parse HTML input and emit tokenizer events in source order.
 * Handles comments/declarations/instructions skipping and script/style block skipping.
 */
void html_parse_lite(const char *html, html_evt_cb_t cb, void *user)
{
    const char *p = html;

    while (*p) {
        const char *t0 = p;
        while (*p && *p != '<') {
            p++;
        }
        emit_text(cb, user, t0, (size_t)(p - t0));

        if (*p == 0) {
            break;
        }

        p++;

        if (startswith_ci(p, HTML_LITE_COMMENT_OPEN)) {
            p += 3;
            while (*p && !(p[0] == '-' && p[1] == '-' && p[2] == '>')) {
                p++;
            }
            if (*p) {
                p += 3;
            }
            continue;
        }

        if (*p == '!') {
            while (*p && *p != '>') {
                p++;
            }
            if (*p == '>') {
                p++;
            }
            continue;
        }

        if (*p == '?') {
            while (*p && !(p[0] == '?' && p[1] == '>')) {
                p++;
            }
            if (*p) {
                p += 2;
            }
            continue;
        }

        bool end_tag = false;
        if (*p == '/') {
            end_tag = true;
            p++;
        }

        skip_ws(&p);

        static char tag[32];
        const char *tag0 = p;
        size_t tlen;
        html_evt_t evt;
        static char scratch[256];

        while (*p && is_name_char(*p)) {
            p++;
        }
        tlen = (size_t)(p - tag0);
        cpy(tag, sizeof(tag), tag0, tlen);

        if (tag[0] == 0) {
            while (*p && *p != '>') {
                p++;
            }
            if (*p == '>') {
                p++;
            }
            continue;
        }

        /* Skip executable/style blocks entirely in this lightweight parser. */
        if (!end_tag && (streq_ci(tag, HTML_LITE_TAG_SCRIPT) || streq_ci(tag, HTML_LITE_TAG_STYLE))) {
            while (*p && *p != '>') {
                p++;
            }
            if (*p == '>') {
                p++;
            }
            {
                const char *close = streq_ci(tag, HTML_LITE_TAG_SCRIPT) ? HTML_LITE_CLOSE_SCRIPT : HTML_LITE_CLOSE_STYLE;
                while (*p && !startswith_ci(p, close)) {
                    p++;
                }
            }
            continue;
        }

        memset(&evt, 0, sizeof(evt));
        evt.tag = tag;
        scratch[0] = 0;

        if (end_tag) {
            while (*p && *p != '>') {
                p++;
            }
            if (*p == '>') {
                p++;
            }
            evt.type = HTML_EVT_END_TAG;
            cb(&evt, user);
            continue;
        }

        parse_attrs(&p, &evt, scratch, sizeof(scratch));

        {
            bool self_close = false;
            skip_ws(&p);
            if (*p == '/' && p[1] == '>') {
                self_close = true;
                p += 2;
            } else {
                while (*p && *p != '>') {
                    p++;
                }
                if (*p == '>') {
                    p++;
                }
            }

            evt.type = self_close ? HTML_EVT_SELF_CLOSING_TAG : HTML_EVT_START_TAG;
            cb(&evt, user);

            if (!self_close) {
                /* Emit synthetic self-closing event for known void tags. */
                if (is_void_tag(tag)) {
                    html_evt_t e2 = evt;
                    e2.type = HTML_EVT_SELF_CLOSING_TAG;
                    cb(&e2, user);
                }
            }
        }
    }
}
