/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_UI_LAYOUT_H
#define BB_UI_LAYOUT_H
#include "address.h"

/* Artwork keeps its original pixel size; only its anchor changes. */
static inline int32_t bb_ui_canvas_x(void) {
  return (BB_I32(0x484444) - 640) / 2;
}
static inline int32_t bb_ui_canvas_y(void) {
  return (BB_I32(0x484448) - 480) / 2;
}
static inline int32_t bb_ui_right(int32_t x) {
  return BB_I32(0x484444) - 640 + x;
}
static inline int32_t bb_ui_bottom(int32_t y) {
  return BB_I32(0x484448) - 480 + y;
}
/* The left toolbar stays put; the profession buttons move to the right.
 * Return an unused original coordinate for the newly exposed stone strip. */
static inline int32_t bb_ui_toolbar_local_x(int32_t x) {
  int32_t extra = BB_I32(0x484444) - 640;
  if (extra > 0 && x >= 380)
    return x >= 433 + extra ? x - extra : 400;
  return x;
}
#endif
