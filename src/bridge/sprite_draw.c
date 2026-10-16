/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include <stddef.h>

#include "render_calls.h"
#include "sprite_models.h"
static uint32_t owner(uint32_t self) { return BB_U32(self + 0x2fa); }
static int32_t wrap(uint32_t value) { return (int32_t)value; }
static uint32_t blit_table(uint32_t table, void *destination, uint32_t x,
                           uint32_t y, void *source, const int32_t *rect,
                           uint32_t flags, uint32_t scratch) {
  if (rect)
    memcpy(BB_SPAN(scratch, 16), rect, 16);
  return BB_RENDER_CALL(BB_U32(table + 0x1c), 0, bb_address_encode(destination),
                        x, y, bb_address_encode(source), rect ? scratch : 0,
                        flags);
}
static uint32_t blit(void *destination, uint32_t x, uint32_t y, void *source,
                     const int32_t *rect, uint32_t flags, uint32_t scratch) {
  return blit_table(BB_U32(bb_address_encode(destination)), destination, x, y,
                    source, rect, flags, scratch);
}

/* 0x00431020..0x0043111b: restore the surface, copy a selected bitmap through
 * GDI, then release both DCs. Return GetDC status, ignoring other API errors.
 */
uint32_t bb_bitmap_copy_c(void *surface, void *bitmap, int32_t x, int32_t y,
                          uint32_t width, uint32_t height, uint32_t scratch) {
  if (!bitmap || !surface)
    return 0x80004005;
  uint32_t surface_ref = bb_address_encode(surface);
  BB_RENDER_METHOD(surface_ref, 0x6c);
  uint32_t source = BB_RENDER_CALL(BB_U32(0x006ca358), 0, 0);
  BB_RENDER_CALL(BB_U32(0x006ca35c), 0, source, bb_address_encode(bitmap));
  uint32_t information = scratch, descriptor = scratch + 24, dc = scratch + 132;
  BB_RENDER_CALL(BB_U32(0x006ca368), 0, bb_address_encode(bitmap), 24,
                 information);
  if (!width)
    width = BB_U32(information + 4);
  if (!height)
    height = BB_U32(information + 8);
  BB_U32(descriptor) = 108;
  BB_U32(descriptor + 4) = 6;
  BB_RENDER_METHOD(surface_ref, 0x58, descriptor);
  uint32_t result = BB_RENDER_METHOD(surface_ref, 0x44, dc);
  if (!result) {
    BB_RENDER_CALL(BB_U32(0x006ca360), 0, BB_U32(dc), 0, 0,
                   BB_U32(descriptor + 12), BB_U32(descriptor + 8), source, x,
                   y, width, height, 0xcc0020);
    BB_RENDER_METHOD(surface_ref, 0x68, BB_U32(dc));
  }
  BB_RENDER_CALL(BB_U32(0x006ca364), 0, source);
  return result;
}

/* 0x0042f5f0..0x0042f673: clear indexed pixels in the first surface. */
uint32_t bb_sprite_clear_c(void *object, uint32_t scratch) {
  uint32_t self = bb_address_encode(object);
  uint32_t descriptor = scratch;
  memset(BB_SPAN(descriptor, 108), 0, 108);
  BB_U32(descriptor) = 108;
  void *surface = BB_POINTER(self + 0x2fe);
  uint32_t result =
      BB_RENDER_METHOD(bb_address_encode(surface), 0x64, 0, descriptor, 1, 0);
  if (result)
    return result;
  uint32_t row = BB_U32(descriptor + 36);
  if (row) {
    for (int32_t y = 0; y < BB_I32(self + 0x48); ++y) {
      for (int32_t x = 0; x < BB_I32(self + 0x44); ++x)
        BB_U8(row + (uint32_t)x) = 0;
      row += BB_U32(descriptor + 16);
    }
  }
  surface = BB_POINTER(self + 0x2fe);
  return BB_RENDER_METHOD(bb_address_encode(surface), 0x80, 0);
}

/* 0x0042f740..0x0042f772 checks the current frame but copies the first one. */
uint32_t BB_THISCALL bb_sprite_copy_back(void *object) {
  uint32_t self = bb_address_encode(object);
  uint32_t frame = BB_U32(self + 0x50);
  if (!BB_POINTER(self + 0x2fe + frame * 4))
    return frame;
  uint32_t context = owner(self);
  if (!context)
    return 0;
  return blit(BB_POINTER(context + 0x38), 0, 0, BB_POINTER(self + 0x2fe), NULL,
              0x10, 0);
}

/* 0x0042f780..0x0042f790 and 0x0042f7a0..0x0042f7b6. Boundary thunks retain
 * the original register results alongside the anchor writes. */
uint32_t bb_sprite_anchor(void *object, uint32_t x, uint32_t y) {
  BB_U32(bb_address_encode(object) + 0x54) = x;
  BB_U32(bb_address_encode(object) + 0x58) = y;
  return x;
}
uint32_t bb_sprite_center(void *object) {
  BB_I32(bb_address_encode(object) + 0x54) =
      BB_I32(bb_address_encode(object) + 0x44) / 2;
  int32_t height = BB_I32(bb_address_encode(object) + 0x48);
  BB_I32(bb_address_encode(object) + 0x58) = height / 2;
  return (uint32_t)(height / 2);
}

/* 0x0042fd90..0x00430157: indexed sprite drawing with inclusive clipping,
 * cached compressed frames, blended drawing and at most three lost retries.
 */
uint32_t bb_sprite_draw_c(void *object, int32_t x, int32_t y, int32_t frame,
                          uint32_t back, uint32_t scratch) {
  uint32_t self = bb_address_encode(object), context = owner(self);
  int32_t left = BB_I32(context), top = BB_I32(context + 4);
  int32_t right = wrap(BB_U32(context + 8) + (uint32_t)left - 1);
  int32_t bottom = wrap(BB_U32(context + 12) + (uint32_t)top - 1);
  BB_U32(self + 0x68) = 1;
  BB_U8(self + 0xec) = BB_U8(0x004842c0);
  if (frame < 255)
    BB_U32(self + 0x50) = (uint32_t)frame;
  if (BB_U32(self + 0x50) >= BB_U32(self + 0x4c))
    BB_U32(self + 0x50) = BB_U32(self + 0x50) % BB_U32(self + 0x4c);
  void *source = BB_U32(self + 0x60)
                     ? BB_POINTER(self + 0x2fe)
                     : BB_POINTER(self + 0x2fe + BB_U32(self + 0x50) * 4);
  if (BB_RENDER_METHOD(bb_address_encode(source), 0x60) == 0x887601c2)
    bb_sprite_restore(object);
  if (BB_U32(self + 0x60) &&
      (BB_U8(self + 0xf4) != (uint32_t)frame ||
       BB_U32(self + 0xf0) != BB_U32(0x00484288) ||
       (BB_U32(self + 0x68) && BB_U8(self + 0xed) != BB_U8(0x004842bc) &&
        !BB_U32(0x00484288))))
    bb_sprite_decode(object);
  context = owner(self);
  void *destination = BB_POINTER(context + (back ? 0x38 : 0x34));
  uint32_t table = BB_U32(bb_address_encode(destination));
  if (BB_RENDER_CALL(BB_U32(table + 0x60), 0, bb_address_encode(destination)) ==
      0x887601c2) {
    BB_RENDER_CALL(BB_U32(table + 0x6c), 0, bb_address_encode(destination));
    bb_display_invalidate(0);
  }
  x = wrap((uint32_t)x - BB_U32(self + 0x54));
  y = wrap((uint32_t)y - BB_U32(self + 0x58));
  int32_t image_right = wrap(BB_U32(self + 0x44) + (uint32_t)x - 1);
  int32_t image_bottom = wrap(BB_U32(self + 0x48) + (uint32_t)y - 1);
  /* Retain EAX from the original's rejected intersection paths. */
  if (left > image_right || top > image_bottom)
    return (uint32_t)image_bottom;
  if (x > right)
    return (uint32_t)right;
  if (y > bottom)
    return (uint32_t)bottom;
  int32_t rect[4] = {0};
  uint32_t attempts = 0;
  int retry;
  do {
    uint32_t result = 0;
    retry = 0;
    if (source && owner(self)) {
      unsigned char blend = BB_U8(self + 0xec);
      if (x >= left && image_right <= right && y >= top &&
          image_bottom <= bottom) {
        if (blend)
          bb_sprite_blend((void *)(uintptr_t)owner(self), source, x, y, object,
                          NULL, 0, blend);
        else
          result = blit_table(table, destination, x, y, source, NULL,
                              BB_U32(self + 0x5c) ? 0x11 : 0x10, scratch);
      } else {
        rect[0] = x < left ? wrap((uint32_t)left - (uint32_t)x) : 0;
        if (x < left)
          x = left;
        rect[1] = y < top ? wrap((uint32_t)top - (uint32_t)y) : 0;
        if (y < top)
          y = top;
        rect[2] =
            wrap(BB_U32(self + 0x44) - 1 -
                 (image_right > right ? (uint32_t)image_right - (uint32_t)right
                                      : 0));
        rect[3] = wrap(BB_U32(self + 0x48) - 1 -
                       (image_bottom > bottom
                            ? (uint32_t)image_bottom - (uint32_t)bottom
                            : 0));
        if (blend) {
          memcpy(BB_SPAN(scratch, 16), rect, 16);
          bb_sprite_blend((void *)(uintptr_t)owner(self), source, x, y, object,
                          (const int32_t *)(uintptr_t)scratch, 0, blend);
        } else {
          rect[2] = wrap((uint32_t)rect[2] + 1);
          rect[3] = wrap((uint32_t)rect[3] + 1);
          if (rect[0] < rect[2] && rect[1] < rect[3])
            result = blit_table(table, destination, x, y, source, rect,
                                BB_U32(self + 0x5c) ? 0x11 : 0x10, scratch);
        }
      }
    }
    if (result == 0x887601c2) {
      bb_sprite_restore(object);
      retry = 1;
    } else if (result) {
      if (result == 0x88760096)
        BB_RENDER_CALL(0x0046bae0, 0, 0x006033d8, 0x004888a0, rect[0], rect[1],
                       rect[2], rect[3]);
      else
        BB_RENDER_CALL(0x0046bae0, 0, 0x006033d8, 0x00488880, result, result);
      BB_RENDER_CALL(BB_U32(0x006ca374), 0, 0x006033d8);
    }
    ++attempts;
  } while (retry && attempts < 3);
  return attempts;
}

static void restore_cursor_surfaces(uint32_t self) {
  for (uint32_t offset = 0x40; offset < 0x4c; offset += 4) {
    void *surface = BB_POINTER(owner(self) + offset);
    if (BB_RENDER_METHOD(bb_address_encode(surface), 0x60) == 0x887601c2) {
      surface = BB_POINTER(owner(self) + offset);
      if (BB_RENDER_METHOD(bb_address_encode(surface), 0x6c))
        break;
    }
  }
}
static void cursor_rectangle(uint32_t self, uint32_t xoff, uint32_t yoff,
                             int32_t rect[4]) {
  rect[0] = rect[1] = 0;
  rect[2] = rect[3] = 47;
  uint32_t context = owner(self);
  if (BB_I32(context + xoff) < 0) {
    rect[2] = wrap(BB_U32(context + xoff) + 47);
    BB_U32(context + xoff) = 0;
  }
  context = owner(self);
  if (BB_I32(context + yoff) < 0) {
    rect[3] = wrap((uint32_t)rect[3] + BB_U32(context + yoff));
    BB_U32(context + yoff) = 0;
  }
  context = owner(self);
  if (wrap(BB_U32(context + xoff) + 48) > wrap(BB_U32(0x00484444) - 1))
    rect[2] = wrap(BB_U32(0x00484444) - BB_U32(context + xoff) - 1);
  if (wrap(BB_U32(context + yoff) + 48) > wrap(BB_U32(0x00484448) - 1))
    rect[3] = wrap(BB_U32(0x00484448) - BB_U32(context + yoff) - 1);
  rect[2] = wrap((uint32_t)rect[2] + 1);
  rect[3] = wrap((uint32_t)rect[3] + 1);
}
static void cursor_reset_copy(uint32_t self) {
  uint32_t context = owner(self);
  blit(BB_POINTER(context + 0x44), 0, 0, BB_POINTER(context + 0x40), NULL, 0x10,
       0);
  context = owner(self);
  BB_U32(context + 0x18) = BB_U32(context + 0x14);
  context = owner(self);
  BB_U32(context + 0x24) = BB_U32(context + 0x20);
}

/* 0x0042f7c0..0x0042f90b: restore the previous cursor backing rectangle. */
uint32_t bb_sprite_cursor_erase_c(void *object, uint32_t scratch) {
  if (BB_U32(0x0048443c) == 5)
    return 5;
  uint32_t self = bb_address_encode(object);
  restore_cursor_surfaces(self);
  uint32_t context = owner(self);
  void *destination = BB_POINTER(context + 0x38);
  if (BB_I32(context + 0x18) > -256) {
    int32_t rect[4];
    cursor_rectangle(self, 0x18, 0x24, rect);
    context = owner(self);
    blit(destination, BB_U32(context + 0x18), BB_U32(context + 0x24),
         BB_POINTER(context + 0x44), rect, 0x10, scratch);
  }
  cursor_reset_copy(self);
  return (uint32_t)owner(self);
}

/* 0x0042f910..0x0042fb45: save the backing pixels and draw the cursor. */
uint32_t bb_sprite_cursor_draw_c(void *object, int32_t x, int32_t y,
                                 uint32_t back, uint32_t scratch) {
  if (BB_U32(0x0048443c) == 5)
    return 5;
  uint32_t self = bb_address_encode(object);
  restore_cursor_surfaces(self);
  x = wrap((uint32_t)x - BB_U32(self + 0x54));
  y = wrap((uint32_t)y - BB_U32(self + 0x58));
  uint32_t context = owner(self);
  void *destination = BB_POINTER(context + (back ? 0x38 : 0x34));
  if (back) {
    BB_U32(context + 0x10) = 0;
    cursor_reset_copy(self);
  }
  if (BB_U32(owner(self) + 0x10)) {
    int32_t rect[4];
    cursor_rectangle(self, 0x14, 0x20, rect);
    context = owner(self);
    blit(destination, BB_U32(context + 0x14), BB_U32(context + 0x20),
         BB_POINTER(context + 0x40), rect, 0x10, scratch);
  }
  int32_t rect[4] = {x < 0 ? 0 : x, y < 0 ? 0 : y, wrap((uint32_t)x + 47),
                     wrap((uint32_t)y + 47)};
  if (rect[2] > wrap(BB_U32(0x00484444) - 1))
    rect[2] = wrap(BB_U32(0x00484444) - 1);
  if (rect[3] > wrap(BB_U32(0x00484448) - 1))
    rect[3] = wrap(BB_U32(0x00484448) - 1);
  rect[2] = wrap((uint32_t)rect[2] + 1);
  rect[3] = wrap((uint32_t)rect[3] + 1);
  blit(BB_POINTER(owner(self) + 0x40), 0, 0, destination, rect, 0x10, scratch);
  BB_U32(owner(self) + 0x14) = (uint32_t)x;
  BB_U32(owner(self) + 0x20) = (uint32_t)y;
  BB_U32(owner(self) + 0x10) = 1;
  return bb_sprite_draw(object, wrap(BB_U32(self + 0x54) + (uint32_t)x),
                        wrap(BB_U32(self + 0x58) + (uint32_t)y),
                        BB_U32(self + 0x50), back);
}

#ifndef BB_MODEL_RUNTIME
#ifdef _WIN32
#define BB_ANCHOR_ASM "_bb_sprite_anchor"
#define BB_CENTER_ASM "_bb_sprite_center"
#else
#define BB_ANCHOR_ASM "bb_sprite_anchor"
#define BB_CENTER_ASM "bb_sprite_center"
#endif
void __attribute__((naked)) bb_sprite_anchor_entry(void) {
  __asm__ volatile("pushl %ecx\n\t"
                   "pushl 12(%esp)\n\t"
                   "pushl 12(%esp)\n\t"
                   "pushl %ecx\n\t"
                   "call " BB_ANCHOR_ASM "\n\t"
                   "addl $12, %esp\n\t"
                   "popl %ecx\n\t"
                   "movl 8(%esp), %edx\n\t"
                   "ret $8");
}
void __attribute__((naked)) bb_sprite_center_entry(void) {
  __asm__ volatile("pushl %ecx\n\t"
                   "call " BB_CENTER_ASM "\n\t"
                   "popl %ecx\n\t"
                   "movl 0x48(%ecx), %edx\n\t"
                   "sarl $31, %edx\n\t"
                   "ret");
}

uint32_t bb_bitmap_copy(void *surface, void *bitmap, int32_t x, int32_t y,
                        uint32_t width, uint32_t height) {
  uint32_t scratch[BB_SPRITE_BITMAP_BYTES / 4];
  return bb_bitmap_copy_c(surface, bitmap, x, y, width, height,
                          bb_address_encode(scratch));
}
uint32_t BB_THISCALL bb_sprite_clear(void *object) {
  uint32_t scratch[BB_SPRITE_DESCRIPTOR_BYTES / 4];
  return bb_sprite_clear_c(object, bb_address_encode(scratch));
}
uint32_t BB_THISCALL bb_sprite_draw(void *object, int32_t x, int32_t y,
                                    int32_t frame, uint32_t back) {
  uint32_t scratch[BB_SPRITE_RECT_BYTES / 4];
  return bb_sprite_draw_c(object, x, y, frame, back,
                          bb_address_encode(scratch));
}
uint32_t BB_THISCALL bb_sprite_cursor_erase(void *object) {
  uint32_t scratch[BB_SPRITE_RECT_BYTES / 4];
  return bb_sprite_cursor_erase_c(object, bb_address_encode(scratch));
}
uint32_t BB_THISCALL bb_sprite_cursor_draw(void *object, int32_t x, int32_t y,
                                           uint32_t back) {
  uint32_t scratch[BB_SPRITE_RECT_BYTES / 4];
  return bb_sprite_cursor_draw_c(object, x, y, back,
                                 bb_address_encode(scratch));
}
#endif
