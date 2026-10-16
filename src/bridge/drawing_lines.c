/* SPDX-License-Identifier: GPL-3.0-only */
#include "interop.h"
#include "world_controls.h"
#ifdef BB_MODEL_RUNTIME
#include "game_calls.h"
#endif
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define PTR(a) ((void *)(uintptr_t)(a))
/* 0x433190..0x433608: eight octants share the same wrapped WORD error
 * calculation. The major-axis gate is signed, the loop count wraps to WORD,
 * and each inclusive point writes center/right/left/below/above in order.
 * These kernels intentionally rely on their caller's valid surface bounds. */
static void line(void *pixels, uint32_t pitch, uint32_t x1, uint32_t y1,
                 uint32_t x2, uint32_t y2, uint32_t color, int x_major,
                 int x_direction, int y_direction) {
  int32_t x = (int16_t)x1, y = (int16_t)y1;
  int32_t end_x = (int16_t)x2, end_y = (int16_t)y2;
  uint32_t p = bb_address_encode(pixels) + (uint32_t)y * pitch + (uint32_t)x;
  int32_t major =
      x_major ? (end_x - x) * x_direction : (end_y - y) * y_direction;
  int32_t minor =
      x_major ? (end_y - y) * y_direction : (end_x - x) * x_direction;
  int16_t twice_minor = (uint16_t)(minor * 2);
  int16_t diagonal = (uint16_t)(minor * 2 - major * 2);
  int16_t error = (uint16_t)(minor * 2 - major);
  if (major < 0)
    return;
  uint16_t count = (uint16_t)(major + 1);
  uint32_t major_step =
      x_major ? (uint32_t)x_direction : pitch * (uint32_t)y_direction;
  uint32_t minor_step =
      x_major ? pitch * (uint32_t)y_direction : (uint32_t)x_direction;
  do {
    BYTE(p) = color;
    BYTE(p + 1) = color;
    BYTE(p - 1) = color;
    BYTE(p + pitch) = color;
    BYTE(p - pitch) = color;
    if (error > 0) {
      error = (uint16_t)(error + diagonal);
      p += minor_step;
    } else {
      error = (uint16_t)(error + twice_minor);
    }
    p += major_step;
  } while (--count);
}
#define LINE(name, major, dx, dy)                                              \
  void BB_STDCALL name(void *pixels, uint32_t pitch, uint32_t x1, uint32_t y1, \
                       uint32_t x2, uint32_t y2, uint32_t color) {             \
    line(pixels, pitch, x1, y1, x2, y2, color, major, dx, dy);                 \
  }
LINE(bb_line_right_down_x_entry, 1, 1, 1)
LINE(bb_line_right_down_y_entry, 0, 1, 1)
LINE(bb_line_left_down_y_entry, 0, -1, 1)
LINE(bb_line_left_down_x_entry, 1, -1, 1)
LINE(bb_line_right_up_x_entry, 1, 1, -1)
LINE(bb_line_right_up_y_entry, 0, 1, -1)
LINE(bb_line_left_up_y_entry, 0, -1, -1)
LINE(bb_line_left_up_x_entry, 1, -1, -1)
#undef LINE

/* 0x433610: Lock succeeds only for HRESULT zero. Unlock uses the originally
 * cached surface vtable, even if Lock replaces the object's live vtable. */
uint32_t BB_THISCALL bb_display_line_entry(void *object, void *surface,
                                           uint32_t x1, uint32_t y1,
                                           uint32_t x2, uint32_t y2,
                                           uint32_t color) {
  (void)object;
#ifdef BB_MODEL_RUNTIME
  struct bb_model_scratch scratch;
  bb_model_scratch_begin(&scratch, 108);
  uint32_t *descriptor = BB_SPAN(scratch.address, 108);
  descriptor[0] = 108;
  uint32_t table = BB_U32(surface);
  if (BB_GAME_CALL(BB_U32(table + 0x64), 0, bb_address_encode(surface), 0,
                   scratch.address, 1, 0)) {
    bb_model_scratch_end(&scratch);
    return 0;
  }
#else
  typedef int32_t(BB_STDCALL * lock_fn)(void *, void *, void *, uint32_t,
                                        uint32_t);
  typedef int32_t(BB_STDCALL * unlock_fn)(void *, void *);
  uint32_t descriptor[27];
  descriptor[0] = 0x6c;
  uintptr_t table = BB_U32((uintptr_t)surface);
  if (((lock_fn)BB_CODE(BB_POINTER(table + 0x64)))(surface, NULL, descriptor, 1,
                                                   0))
    return 0;
#endif
  int32_t x = (int16_t)x1, y = (int16_t)y1;
  int32_t end_x = (int16_t)x2, end_y = (int16_t)y2;
  void *pixels = PTR(descriptor[9]);
  uint32_t pitch = descriptor[4];
  if (x < end_x) {
    if (y < end_y) {
      if (end_x - x > end_y - y)
        bb_line_right_down_x_entry(pixels, pitch, x1, y1, x2, y2, color);
      else
        bb_line_right_down_y_entry(pixels, pitch, x1, y1, x2, y2, color);
    } else if (end_x - x > y - end_y) {
      bb_line_right_up_x_entry(pixels, pitch, x1, y1, x2, y2, color);
    } else {
      bb_line_right_up_y_entry(pixels, pitch, x1, y1, x2, y2, color);
    }
  } else if (y < end_y) {
    if (x - end_x > end_y - y)
      bb_line_left_down_x_entry(pixels, pitch, x1, y1, x2, y2, color);
    else
      bb_line_left_down_y_entry(pixels, pitch, x1, y1, x2, y2, color);
  } else if (x - end_x > y - end_y) {
    bb_line_left_up_x_entry(pixels, pitch, x1, y1, x2, y2, color);
  } else {
    bb_line_left_up_y_entry(pixels, pitch, x1, y1, x2, y2, color);
  }
#ifdef BB_MODEL_RUNTIME
  BB_GAME_CALL(BB_U32(table + 0x80), 0, bb_address_encode(surface), 0);
  bb_model_scratch_end(&scratch);
#else
  ((unlock_fn)BB_CODE(BB_POINTER(table + 0x80)))(surface, NULL);
#endif
  return 1;
}
