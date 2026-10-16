/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include <stddef.h>

#include "render_calls.h"
#include "sprite_models.h"
static int32_t wrap(uint32_t value) { return (int32_t)value; }
static uint32_t lock(uint32_t surface, uint32_t descriptor) {
  return BB_RENDER_METHOD(surface, 0x64, 0, descriptor, 1, 0);
}

/* 0x004327f0..0x00432c3d: indexed pixel effects on the owner's back surface.
 * Modes 1/2 use two 256x256 maps (mode 1 reverses source rows); mode 3 copies
 * pixels except 0x83, which shades the existing destination through a 256 map.
 * All modes skip the source's first pixel value, regardless of transparency.
 */
uint32_t bb_sprite_blend_c(void *owner, void *source, uint32_t x, uint32_t y,
                           void *sprite, const int32_t *rect, uint32_t ignored,
                           unsigned char mode, uint32_t scratch) {
  (void)ignored;
  uint32_t context = bb_address_encode(owner),
           object = bb_address_encode(sprite);
  uint32_t destination_desc = scratch, source_desc = scratch + 108;
  BB_U32(destination_desc) = 108;
  uint32_t result = lock(BB_U32(context + 0x38), destination_desc);
  if (result)
    return result;
  uint32_t destination = BB_U32(destination_desc + 36);
  /* The original leaves the successful destination lock held on these exits. */
  if (!source)
    return (uint32_t)destination;
  BB_U32(source_desc) = 108;
  uint32_t source_ref = bb_address_encode(source),
           source_table = BB_U32(source_ref);
  result = BB_RENDER_CALL(BB_U32(source_table + 0x64), 0, source_ref, 0,
                          source_desc, 1, 0);
  if (result)
    return result;
  if (mode >= 1 && mode <= 3) {
    uint32_t source_step = BB_U32(source_desc + 16);
    uint32_t source_row = BB_U32(source_desc + 36), map;
    uint32_t left = rect ? (uint32_t)BB_I32(bb_address_encode(rect)) : 0;
    uint32_t top = rect ? (uint32_t)BB_I32(bb_address_encode(rect) + 4) : 0;
    if (mode == 1) {
      source_row += (BB_U32(object + 0x48) - top - 1) * source_step + left;
      source_step = 0u - source_step;
      map = context + 0x10ebc;
    } else {
      source_row += top * source_step + left;
      map = context + (mode == 2 ? 0xebc : 0xcbc);
    }
    destination += x + y * BB_U32(destination_desc + 16);
    unsigned char marker = BB_U8(BB_U32(source_desc + 36));
    uint32_t row = top;
    for (;;) {
      if (rect ? wrap(row) > BB_I32(bb_address_encode(rect) + 12)
               : wrap(row) >= BB_I32(object + 0x48))
        break;
      uint32_t input = source_row, output = destination;
      uint32_t column = rect ? (uint32_t)BB_I32(bb_address_encode(rect)) : 0;
      for (;;) {
        if (rect ? wrap(column) > BB_I32(bb_address_encode(rect) + 8)
                 : wrap(column) >= BB_I32(object + 0x44))
          break;
        unsigned char value = BB_U8(input);
        if (value != marker) {
          unsigned char background = BB_U8(output);
          if (mode <= 2)
            BB_U8(output) = BB_U8(map + ((uint32_t)value << 8) + background);
          else
            BB_U8(output) = value == 0x83 ? BB_U8(map + background) : value;
        }
        ++input;
        ++output;
        ++column;
      }
      source_row += source_step;
      destination += BB_U32(destination_desc + 16);
      ++row;
    }
  }
  /* Use the cached source vtable, then reread the owner's destination. */
  BB_RENDER_CALL(BB_U32(source_table + 0x80), 0, source_ref, 0);
  return BB_RENDER_METHOD(BB_U32(context + 0x38), 0x80, 0);
}

/* 0x004325f0..0x004327e0: a source subrectangle, used by font drawing, clipped
 * against its owner's viewport even when another sprite is the destination.
 * The original uses destination+size in its inclusive edge comparisons.
 */
uint32_t bb_sprite_draw_region_c(void *sprite, int32_t source_x,
                                 int32_t source_y, uint32_t width,
                                 uint32_t height, int32_t x, int32_t y,
                                 void *target, uint32_t scratch) {
  uint32_t self = bb_address_encode(sprite);
  uint32_t owner = BB_U32(self + 0x2fa);
  int32_t left = BB_I32(owner), top = BB_I32(owner + 4);
  int32_t right = wrap((uint32_t)left + BB_U32(owner + 8) - 1);
  int32_t bottom = wrap((uint32_t)top + BB_U32(owner + 12) - 1);
  int32_t rect[4] = {source_x, source_y, wrap((uint32_t)source_x + width - 1),
                     wrap((uint32_t)source_y + height - 1)};
  void *destination = target ? BB_POINTER(bb_address_encode(target) + 0x2fe)
                             : BB_POINTER(owner + 0x38);
  int32_t edge_x = wrap((uint32_t)x + width),
          edge_y = wrap((uint32_t)y + height);
  if (left > edge_x)
    return (uint32_t)edge_x;
  if (top > edge_y)
    return (uint32_t)edge_y;
  if (right < x || bottom < y)
    return (uint32_t)right;
  void *source = BB_POINTER(self + 0x2fe + BB_U32(self + 0x50) * 4);
  uint32_t result = (uint32_t)source_x;
  if (source && BB_POINTER(self + 0x2fa)) {
    if (!(x >= left && edge_x <= right && y >= top && edge_y <= bottom)) {
      if (x < left) {
        rect[0] = wrap((uint32_t)source_x - (uint32_t)x + (uint32_t)left);
        x = left;
      }
      if (y < top) {
        rect[1] = wrap((uint32_t)source_y - (uint32_t)y + (uint32_t)top);
        y = top;
      }
      if (edge_x > right)
        rect[2] = wrap((uint32_t)rect[2] + (uint32_t)right - (uint32_t)edge_x);
      if (edge_y > bottom)
        rect[3] = wrap((uint32_t)rect[3] + (uint32_t)bottom - (uint32_t)edge_y);
    }
    rect[2] = wrap((uint32_t)rect[2] + 1);
    rect[3] = wrap((uint32_t)rect[3] + 1);
    memcpy(BB_SPAN(scratch, 16), rect, 16);
    result = BB_RENDER_METHOD(bb_address_encode(destination), 0x1c, x, y,
                              bb_address_encode(source), scratch,
                              BB_U32(self + 0x5c) ? 0x11 : 0x10);
  }
  if (result == 0x887601c2)
    return bb_sprite_restore(sprite);
  return result;
}

#ifndef BB_MODEL_RUNTIME
uint32_t BB_THISCALL bb_sprite_blend(void *object, void *source, uint32_t x,
                                     uint32_t y, void *sprite,
                                     const int32_t *rect, uint32_t ignored,
                                     unsigned char mode) {
  uint32_t scratch[BB_SPRITE_BLEND_BYTES / 4];
  return bb_sprite_blend_c(object, source, x, y, sprite, rect, ignored, mode,
                           bb_address_encode(scratch));
}
uint32_t BB_THISCALL bb_sprite_draw_region(void *object, int32_t source_x,
                                           int32_t source_y, uint32_t width,
                                           uint32_t height, int32_t x,
                                           int32_t y, void *target) {
  uint32_t scratch[BB_SPRITE_RECT_BYTES / 4];
  return bb_sprite_draw_region_c(object, source_x, source_y, width, height, x,
                                 y, target, bb_address_encode(scratch));
}
#endif
