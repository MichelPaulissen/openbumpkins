/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include <string.h>

#include "render_calls.h"
#include "sprite_models.h"

static void describe(uint32_t descriptor, uint32_t self, uint32_t caps) {
  memset(BB_SPAN(descriptor, 108), 0, 108);
  BB_U32(descriptor) = 108;
  BB_U32(descriptor + 4) = 7;
  BB_U32(descriptor + 8) = BB_U32(self + 0x48);
  BB_U32(descriptor + 12) = BB_U32(self + 0x44);
  BB_U32(descriptor + 104) = caps;
}
static uint32_t lock(void *surface, uint32_t descriptor) {
  return BB_RENDER_METHOD(bb_address_encode(surface), 0x64, 0, descriptor, 1,
                          0);
}
static uint32_t unlock(void *surface) {
  return BB_RENDER_METHOD(bb_address_encode(surface), 0x80, 0);
}
static void color_key(void *surface) {
  bb_surface_color_key(surface, UINT32_MAX);
}
static uint32_t member(uint32_t owner, uint32_t name) {
  return BB_RENDER_CALL(0x00466c66, owner, name);
}
static uint32_t read_member(uint32_t view, uint32_t out, uint32_t count) {
  return BB_RENDER_CALL(0x00466e10, view, out, count);
}
static uint32_t release_member(uint32_t view) {
  return BB_RENDER_CALL(0x00466d59, 0x005f6e20, view);
}
static void fatal(uint32_t code) { BB_RENDER_CALL(0x00403a40, 0, code); }
static uint32_t allocate(uint32_t bytes) {
  return BB_RENDER_CALL(0x0046c060, 0, bytes);
}
static uint32_t remove_object(uint32_t object) {
  return BB_RENDER_CALL(0x0046c110, 0, object);
}

/* 0x0042f5a0..0x0042f5dc. Other fields deliberately remain untouched. */
void *bb_sprite_construct(void *object, void *owner) {
  uint32_t self = bb_address_encode(object);
  BB_U32(self + 0x5c) = 1;
  BB_SET_POINTER(self + 0x2fa, owner);
  BB_SET_POINTER(self + 0x2fe, NULL);
  BB_U32(self + 0x4c) = 0;
  BB_U32(self + 0x50) = 0;
  BB_U32(self + 0x54) = 0;
  BB_U32(self + 0x58) = 0;
  BB_U8(self) = 0;
  BB_U8(self + 0xec) = 0;
  BB_U32(self + 0xf0) = 0;
  BB_SET_POINTER(self + 0x2f6, NULL);
  return object;
}

/* 0x004313e0..0x004314f4: a single cleared, offscreen indexed surface.
 * Like the original, surface creation is checked but lock status is ignored.
 */
uint32_t bb_sprite_blank_c(void *object, int32_t width, int32_t height,
                           uint32_t transparent, uint32_t scratch) {
  uint32_t self = bb_address_encode(object);
  BB_I32(self + 0x44) = width;
  BB_I32(self + 0x48) = height;
  BB_U32(self + 0x50) = 0;
  BB_U32(self + 0x5c) = transparent;
  BB_U32(self + 0x60) = 0;
  BB_U32(self + 0x4c) = 1;
  BB_U32(self + 0x64) = 0;
  uint32_t descriptor = scratch;
  describe(descriptor, self, 0x840);
  void *draw = BB_POINTER(0x004eebf8);
  if (BB_RENDER_METHOD(bb_address_encode(draw), 0x18, descriptor, self + 0x2fe,
                       0))
    return 0;
  lock(BB_POINTER(self + 0x2fe), descriptor);
  uint32_t row = BB_U32(descriptor + 36);
  for (int32_t y = 0; y < height; ++y) {
    if (width > 0)
      memset(BB_SPAN(row, (uint32_t)width), 0, (uint32_t)width);
    row += BB_U32(descriptor + 16);
  }
  unlock(BB_POINTER(self + 0x2fe));
  if (BB_U32(self + 0x5c))
    color_key(BB_POINTER(self + 0x2fe));
  BB_U8(self + 0x6c) = 0;
  return 1;
}

/* 0x0042fb50..0x0042fd8f. A marker followed by an eight-bit count encodes
 * transparent runs; count zero encodes 256 pixels. The marker is itself the
 * first run prefix. With transparency disabled the original skips all pixels.
 */
uint32_t bb_sprite_decode_c(void *object, uint32_t scratch) {
  uint32_t self = bb_address_encode(object);
  uint32_t source = BB_U32(self + 0xf6 + 4 * BB_U32(self + 0x50));
  uint32_t descriptor = scratch;
  BB_U32(descriptor) = 108;
  uint32_t result = lock(BB_POINTER(self + 0x2fe), descriptor);
  if (result)
    return result;
  uint32_t table;
  if (BB_U32(0x00484288) && BB_U32(self + 0x64)) {
    table = 0x004ef784;
    BB_U32(self + 0xf0) = 1;
  } else {
    table = 0x006b2020;
    unsigned char base = BB_U8(0x0048877c);
    unsigned char player = BB_U8(0x004842bc);
    unsigned char first = BB_U8(0x0048877c + player);
    for (uint32_t i = 0; i < 7; ++i)
      BB_U8(table + base + i) = (unsigned char)(first + i);
    BB_U32(self + 0xf0) = 0;
    BB_U8(self + 0xed) = BB_U8(0x004842bc);
  }
  if (BB_U32(self + 0x5c)) {
    unsigned char marker = BB_U8(source), saved = BB_U8(table + marker);
    BB_U8(table + marker) = marker;
    uint32_t width = BB_U32(self + 0x44), height = BB_U32(self + 0x48);
    uint32_t padding = BB_U32(descriptor + 16) - width;
    uint32_t pixel = BB_U32(descriptor + 36);
    do {
      uint32_t remaining = width;
      do {
        unsigned char value = BB_U8(source++);
        if (value == marker) {
          unsigned char count = BB_U8(source++);
          do {
            BB_U8(pixel++) = marker;
            --remaining;
          } while (--count);
        } else {
          BB_U8(pixel++) = BB_U8(table + value);
          --remaining;
        }
      } while (remaining);
      pixel += padding;
    } while (--height);
    BB_U8(table + marker) = saved;
  }
  BB_U8(self + 0xf4) = BB_U8(self + 0x50);
  return unlock(BB_POINTER(self + 0x2fe));
}

/* 0x00431500..0x004318bf: load MFB headers and either raw surfaces or
 * compressed frame buffers. Header dimensions govern copying; descriptor
 * dimensions and the loop bound are reread from the shared sprite object.
 */
uint32_t bb_sprite_load_c(void *object, const char *filename,
                          uint32_t scratch) {
  uint32_t self = bb_address_encode(object);
  uint32_t draw = BB_U32(BB_U32(self + 0x2fa) + 0x30);
  uint32_t name = bb_address_encode(filename);
  uint32_t view = member(0x005f6e20, name);
  if (!view) {
    BB_RENDER_CALL(0x0046bae0, 0, 0x006033d8, 0x004888ec, name);
    fatal(3);
  }
  strcpy(BB_DATA(self), BB_DATA(name));
  uint32_t descriptor = scratch + 20;
  read_member(view, scratch, 6);
  BB_U8(scratch + 6) = 0;
  int version = BB_RENDER_CALL(0x0046d230, 0, scratch + 3);
  for (uint32_t i = 0; i < 6; ++i)
    read_member(view, scratch + 8 + i * 2, 2);
  BB_I32(self + 0x44) = BB_I16(scratch + 8);
  BB_I32(self + 0x48) = BB_I16(scratch + 10);
  BB_I32(self + 0x4c) = BB_I16(scratch + 18);
  BB_U32(self + 0x5c) = (BB_I16(scratch + 16) & 1) != 0;
  BB_U32(self + 0x60) = (BB_I16(scratch + 16) & 2) != 0;
  BB_U32(self + 0x64) = (BB_I16(scratch + 16) & 4) != 0;
  if (version < 101 || strstr(BB_DATA(name), "maptile.mfb") ||
      strstr(BB_DATA(name), "hills.mfb")) {
    BB_I32(self + 0x54) = BB_I32(self + 0x44) / 2;
    BB_U32(self + 0x58) = BB_U32(self + 0x48);
  } else {
    BB_I32(self + 0x54) = BB_I16(scratch + 12);
    BB_I32(self + 0x58) = BB_I16(scratch + 14);
  }
  if (!BB_U32(self + 0x60)) {
    uint32_t create = BB_I32(self + 0x4c) > 0 ? BB_U32(BB_U32(draw) + 0x18) : 0;
    for (int32_t i = 0; i < BB_I32(self + 0x4c); ++i) {
      uint32_t slot = self + 0x2fe + 4 * (uint32_t)i;
      describe(descriptor, self, 0x40);
      if (BB_RENDER_CALL(create, 0, draw, descriptor, slot, 0)) {
        release_member(view);
        return 0;
      }
      lock(BB_POINTER(slot), descriptor);
      uint32_t row = BB_U32(descriptor + 36);
      for (int32_t y = 0; y < BB_I16(scratch + 10); ++y) {
        read_member(view, row, (uint32_t)(int32_t)BB_I16(scratch + 8));
        row += BB_U32(descriptor + 16);
      }
      unlock(BB_POINTER(slot));
      if (BB_U32(self + 0x5c))
        color_key(BB_POINTER(slot));
      if (i < 256)
        BB_U8(self + 0x6c + i) = 0;
    }
  } else {
    describe(descriptor, self, 0x40);
    if (BB_RENDER_METHOD(draw, 0x18, descriptor, self + 0x2fe, 0)) {
      release_member(view);
      return 0;
    }
    for (int32_t i = 0; i < BB_I32(self + 0x4c); ++i) {
      read_member(view, scratch + 128, 4);
      uint32_t size = BB_U32(scratch + 128);
      uint32_t slot = self + 0xf6 + 4 * (uint32_t)i;
      BB_U32(slot) = allocate(size);
      if (!BB_POINTER(slot))
        fatal(1);
      read_member(view, BB_U32(slot), size);
      BB_U8(self + 0x6c + i) = 0;
    }
    bb_sprite_decode(object);
    if (BB_U32(self + 0x5c))
      color_key(BB_POINTER(self + 0x2fe));
  }
  release_member(view);
  BB_U32(self + 0xf0) = 0;
  return 1;
}

/* 0x004312d0..0x0043139b: map RGB to the surface's pixel representation by
 * temporarily drawing at (0,0). UINT32_MAX instead reads its existing pixel.
 * Keep the x86 shift-count mask: a 32-bit format produces a zero mask here.
 */
uint32_t bb_surface_pixel_c(void *surface, uint32_t color, uint32_t scratch) {
  uint32_t pixel = UINT32_MAX, previous = 0,
           object = bb_address_encode(surface);
  uint32_t table = 0, dc = scratch + 108;
  if (color != UINT32_MAX) {
    table = BB_U32(object);
    if (!BB_RENDER_CALL(BB_U32(table + 0x44), 0, object, dc)) {
      previous = BB_RENDER_CALL(BB_U32(0x006ca350), 0, BB_U32(dc), 0, 0);
      BB_RENDER_CALL(BB_U32(0x006ca354), 0, BB_U32(dc), 0, 0, color);
      BB_RENDER_CALL(BB_U32(table + 0x68), 0, object, BB_U32(dc));
    }
  }
  BB_U32(scratch) = 108;
  table = BB_U32(object);
  if (!BB_RENDER_CALL(BB_U32(table + 0x64), 0, object, 0, scratch, 1, 0)) {
    pixel = BB_U32(BB_U32(scratch + 36)) &
            ((1u << (BB_U32(scratch + 84) & 31)) - 1);
    BB_RENDER_CALL(BB_U32(table + 0x80), 0, object, 0);
  }
  if (color != UINT32_MAX &&
      !BB_RENDER_CALL(BB_U32(table + 0x44), 0, object, dc)) {
    BB_RENDER_CALL(BB_U32(0x006ca354), 0, BB_U32(dc), 0, 0, previous);
    BB_RENDER_CALL(BB_U32(table + 0x68), 0, object, BB_U32(dc));
  }
  return pixel;
}

/* 0x004313a0..0x004313d2: equal low/high source color keys. */
uint32_t bb_surface_color_key_c(void *surface, uint32_t color,
                                uint32_t scratch) {
  BB_U32(scratch) = bb_surface_pixel(surface, color);
  BB_U32(scratch + 4) = BB_U32(scratch);
  return BB_RENDER_METHOD(bb_address_encode(surface), 0x74, 8, scratch);
}

/* 0x00430f00..0x00431012: module/file bitmap loading and surface conversion.
 * Creation failure retains the original bitmap-handle leak. */
void *bb_bitmap_load_c(void *draw, const char *name, int32_t width,
                       int32_t height, uint32_t *actual_width,
                       uint32_t *actual_height, uint32_t scratch) {
  uint32_t module = BB_RENDER_CALL(BB_U32(0x006ca39c), 0, 0);
  uint32_t name_ref = bb_address_encode(name);
  uint32_t bitmap = BB_RENDER_CALL(BB_U32(0x006ca530), 0, module, name_ref, 0,
                                   width, height, 0x2000);
  if (!bitmap) {
    bitmap = BB_RENDER_CALL(BB_U32(0x006ca530), 0, 0, name_ref, 0, width,
                            height, 0x2010);
    if (!bitmap)
      return NULL;
  }
  uint32_t descriptor = scratch + 24;
  BB_RENDER_CALL(BB_U32(0x006ca368), 0, bitmap, 24, scratch);
  memset(BB_SPAN(descriptor, 108), 0, 108);
  BB_U32(descriptor) = 108;
  BB_U32(descriptor + 4) = 7;
  BB_U32(descriptor + 8) = BB_U32(scratch + 8);
  BB_U32(descriptor + 12) = BB_U32(scratch + 4);
  BB_U32(descriptor + 104) = 0x840;
  BB_U32(bb_address_encode(actual_width)) = BB_U32(scratch + 4);
  BB_U32(bb_address_encode(actual_height)) = BB_U32(scratch + 8);
  if (BB_RENDER_METHOD(bb_address_encode(draw), 0x18, descriptor, scratch + 132,
                       0))
    return NULL;
  void *surface = BB_POINTER(scratch + 132);
  bb_bitmap_copy(surface, (void *)(uintptr_t)bitmap, 0, 0, 0, 0);
  BB_RENDER_CALL(BB_U32(0x006ca36c), 0, bitmap);
  return surface;
}

/* 0x0042f6e0..0x0042f73f: attach the bitmap to a sprite without naming it. */
uint32_t BB_THISCALL bb_sprite_bitmap(void *object, const char *name,
                                      uint32_t transparent) {
  uint32_t self = bb_address_encode(object);
  BB_SET_POINTER(self + 0x2fe,
                 bb_bitmap_load(BB_POINTER(BB_U32(self + 0x2fa) + 0x30), name,
                                0, 0, (uint32_t *)(uintptr_t)(self + 0x44),
                                (uint32_t *)(uintptr_t)(self + 0x48)));
  BB_U32(self + 0x4c) = 1;
  BB_U8(self + 0x6c) = 0;
  BB_U32(self + 0x5c) = transparent;
  if (transparent)
    color_key(BB_POINTER(self + 0x2fe));
  BB_U32(self + 0x60) = 0;
  BB_U32(self + 0x64) = 0;
  return 0;
}

static void *new_sprite(void *owner) {
  void *sprite = (void *)(uintptr_t)allocate(0x686);
  if (sprite) {
    bb_sprite_construct(sprite, owner);
    BB_SET_POINTER(bb_address_encode(sprite) + 0x2f6,
                   BB_POINTER(bb_address_encode(owner) + 0x20ec2));
    BB_SET_POINTER(bb_address_encode(owner) + 0x20ec2, sprite);
  }
  return sprite;
}

/* 0x0042f2c0..0x0042f31d: creation status does not change the factory result.
 */
void *BB_THISCALL bb_sprite_create_blank(void *owner, int32_t width,
                                         int32_t height, uint32_t transparent) {
  void *sprite = new_sprite(owner);
  if (sprite)
    bb_sprite_blank(sprite, width, height, transparent);
  return sprite;
}

/* 0x0042f320..0x0042f407: copy the name before allocation and use the copy for
 * MFB files. Bitmap loading uses the caller's original pointer. */
void *bb_sprite_create_file_c(void *owner, const char *name,
                              uint32_t transparent, uint32_t scratch) {
  char *filename = BB_SPAN(scratch, 256);
  strcpy(filename, BB_DATA(bb_address_encode(name)));
  void *sprite = new_sprite(owner);
  if (!sprite) {
    fatal(1);
    return NULL;
  }
  if (strstr(filename, ".mfb")) {
    if (!bb_sprite_load(sprite, filename)) {
      BB_SET_POINTER(bb_address_encode(owner) + 0x20ec2,
                     BB_POINTER(bb_address_encode(sprite) + 0x2f6));
      remove_object(bb_address_encode(sprite));
      sprite = NULL;
    }
  } else {
    bb_sprite_bitmap(sprite, name, transparent);
  }
  BB_RENDER_CALL(0x00406a60, 0);
  return sprite;
}

/* 0x00430160..0x004302b7: remap raw frames once with the owner's color table.
 * Each transparent frame protects its first pixel and any colors mapping to it.
 * With transparency disabled, zero pixels are still skipped by the original.
 */
uint32_t bb_sprite_remap_c(void *object, uint32_t scratch) {
  uint32_t self = bb_address_encode(object);
  uint32_t result = BB_U32(self + 0x60);
  if (result)
    return result;
  result = BB_U32(self + 0xf0);
  if (result)
    return result;
  BB_U32(self + 0xf0) = 1;
  result = BB_U32(self + 0x4c);
  unsigned char marker = 0;
  for (int32_t i = 0; i < BB_I32(self + 0x4c); ++i) {
    uint32_t slot = self + 0x2fe + 4 * (uint32_t)i;
    uint32_t descriptor = scratch;
    BB_U32(descriptor) = 108;
    if (BB_POINTER(slot) && !lock(BB_POINTER(slot), descriptor)) {
      unsigned char table[256];
      uint32_t source = BB_U32(self + 0x2fa) + 0xbbc;
      for (uint32_t c = 0; c < 256; ++c)
        table[c] = BB_U8(source + c);
      uint32_t row = BB_U32(descriptor + 36);
      if (BB_U32(self + 0x5c)) {
        marker = BB_U8(row);
        table[marker] = marker;
        for (uint32_t c = 0; c < 256; ++c)
          if (table[c] == marker)
            table[c] = (unsigned char)c;
      }
      for (int32_t y = 0; y < BB_I32(self + 0x48); ++y) {
        for (int32_t x = 0; x < BB_I32(self + 0x44); ++x) {
          uint32_t pixel = row + (uint32_t)x;
          if (BB_U8(pixel) != marker)
            BB_U8(pixel) = table[BB_U8(pixel)];
        }
        row += BB_U32(descriptor + 16);
      }
      unlock(BB_POINTER(slot));
    }
    result = (uint32_t)(slot + 4);
  }
  return result;
}

/* 0x004318c0..0x00431ae8: reopen raw data or re-decode cached compressed data.
 * A failed raw Restore stops further frames but still returns success and
 * applies the final remap. The compressed path instead returns failure.
 */
uint32_t bb_sprite_restore_mfb_c(void *object, uint32_t scratch) {
  uint32_t self = bb_address_encode(object);
  uint32_t view = member(0x005f6e20, self);
  if (!view)
    return 0;
  uint32_t descriptor = scratch + 20;
  read_member(view, scratch, 6);
  BB_U8(scratch + 6) = 0;
  BB_RENDER_CALL(0x0046d230, 0, scratch + 3);
  for (uint32_t i = 0; i < 6; ++i)
    read_member(view, scratch + 8 + i * 2, 2);
  if (!BB_U32(self + 0x60)) {
    for (int32_t i = 0; i < BB_I32(self + 0x4c); ++i) {
      uint32_t slot = self + 0x2fe + 4 * (uint32_t)i;
      if (BB_RENDER_METHOD(BB_U32(slot), 0x60) == 0x887601c2 &&
          BB_RENDER_METHOD(BB_U32(slot), 0x6c))
        break;
      BB_U32(descriptor) = 108;
      lock(BB_POINTER(slot), descriptor);
      uint32_t row = BB_U32(descriptor + 36);
      for (int32_t y = 0; y < BB_I16(scratch + 10); ++y) {
        read_member(view, row, (uint32_t)(int32_t)BB_I16(scratch + 8));
        row += BB_U32(descriptor + 16);
      }
      unlock(BB_POINTER(slot));
      if (BB_U32(self + 0x5c))
        color_key(BB_POINTER(slot));
      if (i < 256)
        BB_U8(self + 0x6c + i) = 0;
    }
  } else {
    if (BB_RENDER_METHOD(BB_U32(self + 0x2fe), 0x60) == 0x887601c2 &&
        BB_RENDER_METHOD(BB_U32(self + 0x2fe), 0x6c)) {
      release_member(view);
      return 0;
    }
    bb_sprite_decode(object);
    if (BB_U32(self + 0x5c))
      color_key(BB_POINTER(self + 0x2fe));
  }
  release_member(view);
  BB_U32(self + 0xf0) = 0;
  if (BB_U32(0x00484288) && !BB_U32(self + 0x60) && BB_U32(self + 0x64))
    bb_sprite_remap(object);
  return 1;
}

/* 0x0042f680..0x0042f6d4: non-MFB resources restore their first surface only.
 */
uint32_t BB_THISCALL bb_sprite_restore(void *object) {
  uint32_t self = bb_address_encode(object);
  if (strstr(BB_DATA(self), ".mfb"))
    return bb_sprite_restore_mfb(object);
  void *surface = BB_POINTER(self + 0x2fe);
  if (!surface)
    return 0;
  uint32_t result = BB_RENDER_METHOD(bb_address_encode(surface), 0x60);
  if (result == 0x887601c2) {
    surface = BB_POINTER(self + 0x2fe);
    result = BB_RENDER_METHOD(bb_address_encode(surface), 0x6c);
    if (result)
      result = BB_RENDER_CALL(BB_U32(0x006ca374), 0, 0x00488854);
  }
  return result;
}

/* 0x00433120..0x0043318a: unlink, release the first surface, delete the object.
 * Other raw surfaces/compressed buffers are not freed by the original routine.
 */
uint32_t bb_sprite_remove(void *owner, void *object) {
  uint32_t ref = bb_address_encode(object),
           link = bb_address_encode(owner) + 0x20ec2;
  while (BB_U32(link) != ref)
    link = BB_U32(link) + 0x2f6;
  BB_U32(link) = BB_U32(ref + 0x2f6);
  BB_RENDER_METHOD(BB_U32(ref + 0x2fe), 8);
  return remove_object(ref);
}

#ifndef BB_MODEL_RUNTIME
#ifdef _WIN32
#define BB_CONSTRUCT_ASM "_bb_sprite_construct"
#define BB_REMOVE_ASM "_bb_sprite_remove"
#else
#define BB_CONSTRUCT_ASM "bb_sprite_construct"
#define BB_REMOVE_ASM "bb_sprite_remove"
#endif
void __attribute__((naked)) bb_sprite_construct_entry(void) {
  __asm__ volatile("pushl %edx\n\t"
                   "pushl %ecx\n\t"
                   "pushl 12(%esp)\n\t"
                   "pushl %ecx\n\t"
                   "call " BB_CONSTRUCT_ASM "\n\t"
                   "addl $8, %esp\n\t"
                   "popl %ecx\n\t"
                   "popl %edx\n\t"
                   "ret $4");
}
void __attribute__((naked)) bb_sprite_destroy_entry(void) {
  __asm__ volatile("ret");
}
void __attribute__((naked)) bb_sprite_remove_entry(void) {
  __asm__ volatile("cmpl $0, 4(%esp)\n\t"
                   "je 1f\n\t"
                   "pushl 4(%esp)\n\t"
                   "pushl %ecx\n\t"
                   "call " BB_REMOVE_ASM "\n\t"
                   "addl $8, %esp\n"
                   "1: ret $4");
}

uint32_t BB_THISCALL bb_sprite_blank(void *object, int32_t width,
                                     int32_t height, uint32_t transparent) {
  uint32_t scratch[BB_SPRITE_DESCRIPTOR_BYTES / 4];
  return bb_sprite_blank_c(object, width, height, transparent,
                           bb_address_encode(scratch));
}
uint32_t BB_THISCALL bb_sprite_decode(void *object) {
  uint32_t scratch[BB_SPRITE_DESCRIPTOR_BYTES / 4];
  return bb_sprite_decode_c(object, bb_address_encode(scratch));
}
uint32_t BB_THISCALL bb_sprite_load(void *object, const char *filename) {
  uint32_t scratch[BB_SPRITE_LOAD_BYTES / 4];
  return bb_sprite_load_c(object, filename, bb_address_encode(scratch));
}
uint32_t bb_surface_pixel(void *surface, uint32_t color) {
  uint32_t scratch[BB_SPRITE_PIXEL_BYTES / 4];
  return bb_surface_pixel_c(surface, color, bb_address_encode(scratch));
}
uint32_t bb_surface_color_key(void *surface, uint32_t color) {
  uint32_t scratch[8 / 4];
  return bb_surface_color_key_c(surface, color, bb_address_encode(scratch));
}
void *bb_bitmap_load(void *draw, const char *name, int32_t width,
                     int32_t height, uint32_t *actual_width,
                     uint32_t *actual_height) {
  uint32_t scratch[BB_SPRITE_BITMAP_BYTES / 4];
  return bb_bitmap_load_c(draw, name, width, height, actual_width,
                          actual_height, bb_address_encode(scratch));
}
void *BB_THISCALL bb_sprite_create_file(void *object, const char *name,
                                        uint32_t transparent) {
  uint32_t scratch[BB_SPRITE_FILENAME_BYTES / 4];
  return bb_sprite_create_file_c(object, name, transparent,
                                 bb_address_encode(scratch));
}
uint32_t BB_THISCALL bb_sprite_remap(void *object) {
  uint32_t scratch[BB_SPRITE_DESCRIPTOR_BYTES / 4];
  return bb_sprite_remap_c(object, bb_address_encode(scratch));
}
uint32_t BB_THISCALL bb_sprite_restore_mfb(void *object) {
  uint32_t scratch[BB_SPRITE_RESTORE_BYTES / 4];
  return bb_sprite_restore_mfb_c(object, bb_address_encode(scratch));
}
#endif
