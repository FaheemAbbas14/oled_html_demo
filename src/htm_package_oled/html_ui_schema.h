#ifndef HTML_UI_SCHEMA_H
#define HTML_UI_SCHEMA_H

/* Centralized HTML UI tag and attribute keys. */

/* Tags */
#define HTML_TAG_DIV "div"              /* Generic container; supports layout/style and can wrap child nodes. */
#define HTML_TAG_LABEL "label"          /* Static text element shown on screen. */
#define HTML_TAG_BUTTON "button"        /* Clickable/touchable action element. */
#define HTML_TAG_CARD "card"            /* Grouped visual container (panel-like block). */
#define HTML_TAG_ICON "icon"            /* Icon glyph/image element. */
#define HTML_TAG_PROGRESS "progress"    /* Progress bar/value visualization element. */
#define HTML_TAG_ROW "row"              /* Horizontal flow container for child elements. */
#define HTML_TAG_COLUMN "column"        /* Vertical flow container for child elements. */

/* Common type values */
#define HTML_TYPE_BUTTON "button"       /* Equivalent semantic type for button behavior/style paths. */
#define HTML_TYPE_CARD "card"           /* Equivalent semantic type for card rendering path. */
#define HTML_TYPE_PROGRESS "progress"   /* Equivalent semantic type for progress rendering path. */
#define HTML_TYPE_ICON "icon"           /* Equivalent semantic type for icon rendering path. */
#define HTML_TYPE_ROW "row"             /* Equivalent semantic type for horizontal flow container. */
#define HTML_TYPE_COLUMN "column"       /* Equivalent semantic type for vertical flow container. */

/* Gravity values */
#define HTML_GRAVITY_LEFT "left"        /* Snap/align element toward parent left edge. */
#define HTML_GRAVITY_RIGHT "right"      /* Snap/align element toward parent right edge. */
#define HTML_GRAVITY_TOP "top"          /* Snap/align element toward parent top edge. */
#define HTML_GRAVITY_BOTTOM "bottom"    /* Snap/align element toward parent bottom edge. */

/* Attributes */
#define HTML_ATTR_ID "id"                               /* Stable element identifier for lookup/debug. */
#define HTML_ATTR_TYPE "data-type"                      /* Explicit semantic type override for renderer. */
#define HTML_ATTR_ACTION "data-action"                  /* Action key triggered on interaction (e.g., button press). */
#define HTML_ATTR_TARGET_SCREEN "data-target-html-screen" /* Target screen/page name for navigation action. */
#define HTML_ATTR_TAG "data-tag"                        /* Generic metadata tag passed with events. */
#define HTML_ATTR_KEY "data-key"                        /* Localization key or lookup key for dynamic text/value. */

#define HTML_ATTR_X "data-x"                            /* Absolute X position in pixels. */
#define HTML_ATTR_Y "data-y"                            /* Absolute Y position in pixels. */
#define HTML_ATTR_W "data-w"                            /* Absolute width in pixels. */
#define HTML_ATTR_H "data-h"                            /* Absolute height in pixels. */
#define HTML_ATTR_XP "data-xp"                          /* X position as percent of parent/container width. */
#define HTML_ATTR_YP "data-yp"                          /* Y position as percent of parent/container height. */
#define HTML_ATTR_WP "data-wp"                          /* Width as percent of parent/container width. */
#define HTML_ATTR_HP "data-hp"                          /* Height as percent of parent/container height. */

#define HTML_ATTR_GRAVITY "data-gravity"                /* Combined gravity shorthand (both axes when applicable). */
#define HTML_ATTR_GRAVITY_X "data-gravity-x"            /* Horizontal gravity override: left/right. */
#define HTML_ATTR_GRAVITY_Y "data-gravity-y"            /* Vertical gravity override: top/bottom. */

#define HTML_ATTR_FLOW_P "data-flowp"                   /* Child size percent used by row/column auto-flow. */
#define HTML_ATTR_ROW_WP "data-row-wp"                  /* Child width percent when parent layout is row. */
#define HTML_ATTR_COLUMN_HP "data-column-hp"            /* Child height percent when parent layout is column. */

#define HTML_ATTR_TEXT_COLOR "data-text-color"          /* Text color for labels/buttons and textual content. */
#define HTML_ATTR_TEXT_COLOR_LIGHT "data-text-color-light" /* Text color override used when light mode is active. */
#define HTML_ATTR_TEXT_COLOR_DARK "data-text-color-dark" /* Text color override used when dark mode is active. */
#define HTML_ATTR_BG_COLOR "data-bg-color"              /* Background fill color for container-like elements. */
#define HTML_ATTR_BG_COLOR_LIGHT "data-bg-color-light"  /* Background color override used when light mode is active. */
#define HTML_ATTR_BG_COLOR_DARK "data-bg-color-dark"    /* Background color override used when dark mode is active. */
#define HTML_ATTR_BORDER_COLOR "data-border-color"      /* Border/outline color where supported. */
#define HTML_ATTR_BORDER_COLOR_LIGHT "data-border-color-light" /* Border color override used when light mode is active. */
#define HTML_ATTR_BORDER_COLOR_DARK "data-border-color-dark" /* Border color override used when dark mode is active. */
#define HTML_ATTR_FILL_COLOR "data-fill-color"          /* Foreground fill color (e.g., progress fill). */
#define HTML_ATTR_FILL_COLOR_LIGHT "data-fill-color-light" /* Fill color override used when light mode is active. */
#define HTML_ATTR_FILL_COLOR_DARK "data-fill-color-dark" /* Fill color override used when dark mode is active. */
#define HTML_ATTR_ICON "data-icon"                      /* Icon key/name to draw in icon-capable elements. */

#define HTML_ATTR_MARGIN "data-margin"                  /* Uniform margin in pixels on all sides. */
#define HTML_ATTR_MARGIN_X "data-margin-x"              /* Horizontal margin in pixels (left + right). */
#define HTML_ATTR_MARGIN_Y "data-margin-y"              /* Vertical margin in pixels (top + bottom). */
#define HTML_ATTR_MARGIN_TOP "data-margin-top"          /* Top margin in pixels. */
#define HTML_ATTR_MARGIN_RIGHT "data-margin-right"      /* Right margin in pixels. */
#define HTML_ATTR_MARGIN_BOTTOM "data-margin-bottom"    /* Bottom margin in pixels. */
#define HTML_ATTR_MARGIN_LEFT "data-margin-left"        /* Left margin in pixels. */
#define HTML_ATTR_MARGIN_P "data-margin-p"              /* Uniform margin as percent of parent size. */
#define HTML_ATTR_MARGIN_XP "data-margin-xp"            /* Horizontal margin as percent of parent width. */
#define HTML_ATTR_MARGIN_YP "data-margin-yp"            /* Vertical margin as percent of parent height. */
#define HTML_ATTR_MARGIN_TOP_P "data-margin-top-p"      /* Top margin as percent of parent height. */
#define HTML_ATTR_MARGIN_RIGHT_P "data-margin-right-p"  /* Right margin as percent of parent width. */
#define HTML_ATTR_MARGIN_BOTTOM_P "data-margin-bottom-p" /* Bottom margin as percent of parent height. */
#define HTML_ATTR_MARGIN_LEFT_P "data-margin-left-p"    /* Left margin as percent of parent width. */

#define HTML_ATTR_PADDING "data-padding"                /* Uniform inner padding in pixels on all sides. */
#define HTML_ATTR_PADDING_X "data-padding-x"            /* Horizontal inner padding in pixels. */
#define HTML_ATTR_PADDING_Y "data-padding-y"            /* Vertical inner padding in pixels. */
#define HTML_ATTR_PADDING_TOP "data-padding-top"        /* Top inner padding in pixels. */
#define HTML_ATTR_PADDING_RIGHT "data-padding-right"    /* Right inner padding in pixels. */
#define HTML_ATTR_PADDING_BOTTOM "data-padding-bottom"  /* Bottom inner padding in pixels. */
#define HTML_ATTR_PADDING_LEFT "data-padding-left"      /* Left inner padding in pixels. */
#define HTML_ATTR_PADDING_P "data-padding-p"            /* Uniform inner padding as percent of parent size. */
#define HTML_ATTR_PADDING_XP "data-padding-xp"          /* Horizontal inner padding as percent of parent width. */
#define HTML_ATTR_PADDING_YP "data-padding-yp"          /* Vertical inner padding as percent of parent height. */
#define HTML_ATTR_PADDING_TOP_P "data-padding-top-p"    /* Top inner padding as percent of parent height. */
#define HTML_ATTR_PADDING_RIGHT_P "data-padding-right-p" /* Right inner padding as percent of parent width. */
#define HTML_ATTR_PADDING_BOTTOM_P "data-padding-bottom-p" /* Bottom inner padding as percent of parent height. */
#define HTML_ATTR_PADDING_LEFT_P "data-padding-left-p"  /* Left inner padding as percent of parent width. */

#define HTML_ATTR_LEFT "data-left"                      /* Anchor offset from parent left edge in pixels. */
#define HTML_ATTR_RIGHT "data-right"                    /* Anchor offset from parent right edge in pixels. */
#define HTML_ATTR_TOP "data-top"                        /* Anchor offset from parent top edge in pixels. */
#define HTML_ATTR_BOTTOM "data-bottom"                  /* Anchor offset from parent bottom edge in pixels. */
#define HTML_ATTR_LEFTP "data-leftp"                    /* Anchor offset from parent left edge in percent. */
#define HTML_ATTR_RIGHTP "data-rightp"                  /* Anchor offset from parent right edge in percent. */
#define HTML_ATTR_TOPP "data-topp"                      /* Anchor offset from parent top edge in percent. */
#define HTML_ATTR_BOTTOMP "data-bottomp"                /* Anchor offset from parent bottom edge in percent. */

#define HTML_ATTR_RADIUS "data-radius"                  /* Corner radius in pixels; 0 means sharp corners. */
#define HTML_ATTR_VALUE "data-value"                    /* Current numeric value (e.g., progress current). */
#define HTML_ATTR_MAX "data-max"                        /* Maximum numeric value for normalized displays. */

#define HTML_ATTR_SIZE "size"                           /* Font/icon size selector for supported size buckets. */

#endif
