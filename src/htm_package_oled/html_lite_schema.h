#ifndef HTML_LITE_SCHEMA_H
#define HTML_LITE_SCHEMA_H

/* Centralized lightweight parser tag/marker keys. */

#define HTML_LITE_COMMENT_OPEN "!--"       /* Comment opener used after '<' for HTML comments. */

#define HTML_LITE_TAG_SCRIPT "script"      /* Non-UI content tag; parser skips script body. */
#define HTML_LITE_TAG_STYLE "style"        /* Non-UI content tag; parser skips style body. */

#define HTML_LITE_CLOSE_SCRIPT "</script"  /* Closing marker for fast script-skip scan. */
#define HTML_LITE_CLOSE_STYLE "</style"    /* Closing marker for fast style-skip scan. */

#define HTML_LITE_TAG_BR "br"              /* Void tag: line break, no closing tag expected. */
#define HTML_LITE_TAG_IMG "img"            /* Void tag: image/icon element, no closing tag expected. */
#define HTML_LITE_TAG_HR "hr"              /* Void tag: horizontal rule, no closing tag expected. */
#define HTML_LITE_TAG_META "meta"          /* Void tag: metadata element, no closing tag expected. */
#define HTML_LITE_TAG_LINK "link"          /* Void tag: resource link element, no closing tag expected. */
#define HTML_LITE_TAG_INPUT "input"        /* Void tag: input element, no closing tag expected. */

#endif
