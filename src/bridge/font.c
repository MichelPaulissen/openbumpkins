/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include "render_calls.h"
#include "sprite_models.h"
#include <string.h>
uint32_t bb_sprite_remove(void *, void *);

/* A font owns a sprite pointer followed by 256 packed 28-byte glyph records:
 * present, source x/y, width/height, x/y offsets. Spacing and fallback
 * dimensions follow at 0x1c04, 0x1c08 and 0x1c0c. Uninitialized glyph fields
 * stay untouched.
 */
static int32_t wrap(uint32_t value) { return (int32_t)value; }
static uint32_t glyph(uint32_t font, unsigned char value) {
  return font + (uint32_t)value * 28;
}
static unsigned char character(uint32_t font, unsigned char value) {
  if (!BB_U32(glyph(font, value) + 4)) {
    if (value >= 'a' && value <= 'z')
      value -= 32;
    else if (value >= 'A' && value <= 'Z')
      value += 32;
  }
  return value;
}
static uint32_t advance(uint32_t font, uint32_t entry) {
  return BB_U32(font + 0x1c04) +
         (BB_U32(entry + 4) ? BB_U32(entry + 0x10) : BB_U32(font + 0x1c08));
}
static uint32_t text_width(uint32_t font, const unsigned char *text) {
  uint32_t width = 0;
  for (uint32_t i = 0; text[i]; ++i)
    if (text[i] >= 32)
      width += advance(font, glyph(font, character(font, text[i])));
  return width;
}
static int32_t aligned(uint32_t font, const unsigned char *text, int32_t x,
                       uint32_t alignment) {
  if (!alignment)
    return x;
  int32_t width = wrap(text_width(font, text));
  if (alignment == 1)
    width /= 2;
  return alignment == 1 || alignment == 2 ? wrap((uint32_t)x - (uint32_t)width)
                                          : x;
}

/* 0x004302c0..0x004302e6. */
void *bb_font_construct(void *object) {
  uint32_t font = bb_address_encode(object);
  BB_U32(font) = 0;
  for (uint32_t i = 0; i < 256; ++i)
    BB_U32(font + 4 + 28 * i) = 0;
  BB_U32(font + 0x1c04) = 0;
  return object;
}
/* 0x004302f0..0x00430308: delete the sprite without unlinking its owner list.
 */
uint32_t bb_font_destroy(void *object) {
  return BB_RENDER_CALL(0x0046c110, 0, BB_U32(bb_address_encode(object)));
}

/* 0x00430310..0x00430540: replace the sprite and read its filename+'.fdf'.
 * A missing directory member returns zero and retains the temporary allocation.
 * The six numeric fields on malformed input are indeterminate in the original;
 * zero initialization makes only that otherwise undefined path deterministic.
 */
uint32_t bb_font_load_c(void *object, void *owner, const char *name,
                        uint32_t scratch) {
  uint32_t font = bb_address_encode(object);
  if (BB_POINTER(font))
    bb_sprite_remove((void *)(uint32_t)0x004eebc8, BB_POINTER(font));
  BB_SET_POINTER(font, bb_sprite_create_file(owner, name, 1));
  uint32_t buffer = BB_RENDER_CALL(0x0046c060, 0, 1024);
  if (!buffer)
    BB_RENDER_CALL(0x00403a40, 0, 1);
  strcpy(BB_DATA(buffer), BB_DATA(bb_address_encode(name)));
  strcat(BB_DATA(buffer), BB_DATA(0x004888d8));
  uint32_t view = BB_RENDER_CALL(0x00466c66, 0x005e4198, buffer);
  if (!view)
    return 0;
  memset(BB_SPAN(scratch, 32), 0, 32);
  uint32_t result;
  do {
    uint32_t cursor = buffer;
    result = BB_RENDER_CALL(0x00466e10, view, scratch + 24, 1);
    while (BB_U8(scratch + 24) == '\n' && result)
      result = BB_RENDER_CALL(0x00466e10, view, scratch + 24, 1);
    while (BB_U8(scratch + 24) != '\n' && result) {
      BB_U8(cursor++) = BB_U8(scratch + 24);
      result = BB_RENDER_CALL(0x00466e10, view, scratch + 24, 1);
    }
    BB_U8(cursor) = 0;
    if (BB_U8(buffer)) {
      BB_RENDER_CALL(0x0046d300, 0, buffer, 0x004888bc, scratch + 25, scratch,
                     scratch + 4, scratch + 8, scratch + 12, scratch + 16,
                     scratch + 20);
      uint32_t entry = glyph(font, BB_U8(scratch + 25));
      BB_U32(entry + 4) = 1;
      BB_U32(entry + 8) = BB_U32(scratch);
      BB_U32(entry + 12) = BB_U32(scratch + 4);
      BB_U32(entry + 16) = BB_U32(scratch + 8);
      BB_U32(entry + 20) = BB_U32(scratch + 12);
      BB_U32(entry + 24) = BB_U32(scratch + 16) - BB_U32(scratch);
      BB_U32(entry + 28) = BB_U32(scratch + 20) - BB_U32(scratch + 4);
    }
    BB_U8(buffer) = 0;
  } while (result);
  BB_RENDER_CALL(0x00466d59, 0x005e4198, view);
  BB_RENDER_CALL(0x0046c110, 0, buffer);
  uint32_t entry = glyph(font, BB_U32(glyph(font, 'a') + 4) ? 'a' : 'A');
  BB_U32(font + 0x1c08) = BB_U32(entry + 0x10);
  result = BB_U32(entry + 0x14);
  BB_U32(font + 0x1c0c) = result;
  return result;
}

/* 0x00430560..0x004306dc: controls are skipped, missing letters try the
 * opposite case, other missing characters use fallback width. Alignment 1/2
 * centers or right-aligns; other values retain x. EAX finishes at the input
 * byte count.
 */
uint32_t BB_THISCALL bb_font_draw(void *object, const char *string, int32_t x,
                                  int32_t y, uint32_t alignment) {
  uint32_t font = bb_address_encode(object);
  const unsigned char *text = BB_DATA(string);
  x = aligned(font, text, x, alignment);
  uint32_t i = 0;
  for (; text[i]; ++i)
    if (text[i] >= 32) {
      uint32_t entry = glyph(font, character(font, text[i]));
      if (BB_U32(entry + 4)) {
        bb_sprite_draw_region(BB_POINTER(font), BB_I32(entry + 8),
                              BB_I32(entry + 12), BB_U32(entry + 16),
                              BB_U32(entry + 20),
                              wrap((uint32_t)x - BB_U32(entry + 24)),
                              wrap((uint32_t)y - BB_U32(entry + 28)), NULL);
        x = wrap((uint32_t)x + BB_U32(font + 0x1c04) + BB_U32(entry + 16));
      } else {
        x = wrap((uint32_t)x + BB_U32(font + 0x1c04) + BB_U32(font + 0x1c08));
      }
    }
  return i;
}

/* Shared original scratch state contains 64 counts, 64x256 text bytes and 64
 * widths, in two separate banks for drawing and measuring. Wrapping truncates
 * at the last space/newline and resumes immediately after that input byte.
 * Only initialized entries are changed; truncated bytes remain in the bank.
 */
static uint32_t lines(uint32_t font, const unsigned char *text, int32_t limit,
                      uint32_t bank) {
  uint32_t line = 0, width = 0, delimiter = 0, restart = 0;
  BB_U32(bank) = 0;
  BB_U32(bank + 0x4100) = 0;
  for (uint32_t i = 0; text[i]; ++i) {
    unsigned char value = text[i];
    if (value < 32 && value != '\n')
      continue;
    value = character(font, value);
    uint32_t count = BB_U32(bank + line * 4);
    BB_U32(bank + line * 4) = count + 1;
    BB_U8(bank + 0x100 + line * 256 + count) = value;
    if (value == ' ' || value == '\n') {
      BB_U32(bank + 0x4100 + line * 4) = width;
      delimiter = wrap(count) < 0 ? 0 : count;
      restart = i;
    }
    width += advance(font, glyph(font, value));
    if (wrap(width) > limit || value == '\n') {
      BB_U32(bank + (line + 1) * 4) = 0;
      BB_U8(bank + 0x100 + line * 256 + delimiter) = 0;
      ++line;
      width = 0;
      i = restart;
    }
  }
  BB_U32(bank + 0x4100 + line * 4) = width;
  BB_U8(bank + 0x100 + line * 256 + BB_U32(bank + line * 4)) = 0;
  return line + 1;
}
static int32_t line_x(int32_t x, int32_t limit, int32_t width,
                      uint32_t alignment, int32_t previous) {
  switch (alignment) {
  case 0:
    return x;
  case 1:
    return wrap((uint32_t)x + (uint32_t)(limit / 2) - (uint32_t)(width / 2));
  case 2:
    return wrap((uint32_t)x + (uint32_t)limit - (uint32_t)width);
  default:
    return previous;
  }
}

/* 0x004306e0..0x004309ae. The line height is read again after drawing each
 * line. Overflow before the first delimiter consumes indeterminate original
 * locals; delimiter/restart initialize to zero only for that otherwise
 * undefined path.
 */
uint32_t BB_THISCALL bb_font_draw_wrapped(void *object, const char *string,
                                          int32_t x, int32_t y, int32_t limit,
                                          uint32_t alignment, void *target) {
  uint32_t font = bb_address_encode(object), bank = 0x006b2120;
  uint32_t count = lines(font, BB_DATA(string), limit, bank);
  int32_t cursor = x;
  for (uint32_t line = 0; line < count; ++line) {
    cursor =
        line_x(x, limit, BB_I32(bank + 0x4100 + line * 4), alignment, cursor);
    unsigned char *text = BB_DATA(bank + 0x100 + line * 256);
    for (uint32_t i = 0; text[i]; ++i) {
      uint32_t entry = glyph(font, character(font, text[i]));
      if (BB_U32(entry + 4)) {
        bb_sprite_draw_region(BB_POINTER(font), BB_I32(entry + 8),
                              BB_I32(entry + 12), BB_U32(entry + 16),
                              BB_U32(entry + 20),
                              wrap((uint32_t)cursor - BB_U32(entry + 24)),
                              wrap((uint32_t)y - BB_U32(entry + 28)), target);
        cursor =
            wrap((uint32_t)cursor + BB_U32(font + 0x1c04) + BB_U32(entry + 16));
      } else {
        cursor = wrap((uint32_t)cursor + BB_U32(font + 0x1c04) +
                      BB_U32(font + 0x1c08));
      }
    }
    y = wrap((uint32_t)y + BB_U32(font + 0x1c0c) * 2);
  }
  return 0;
}

static void include(uint32_t entry, int32_t x, int32_t y, int32_t bounds[4]) {
  int32_t left = wrap((uint32_t)x - BB_U32(entry + 24));
  int32_t top = wrap((uint32_t)y - BB_U32(entry + 28));
  int32_t right = wrap((uint32_t)left + BB_U32(entry + 16));
  int32_t bottom = wrap((uint32_t)top + BB_U32(entry + 20));
  if (left < bounds[0])
    bounds[0] = left;
  if (top < bounds[1])
    bounds[1] = top;
  if (right > bounds[2])
    bounds[2] = right;
  if (bottom > bounds[3])
    bounds[3] = bottom;
}

/* 0x004309b0..0x00430ba8: include the alignment origin even for empty strings.
 */
uint32_t BB_THISCALL bb_font_bounds(void *object, const char *string, int32_t x,
                                    int32_t y, uint32_t alignment,
                                    int32_t *left, int32_t *top, int32_t *right,
                                    int32_t *bottom) {
  uint32_t font = bb_address_encode(object);
  const unsigned char *text = BB_DATA(string);
  x = aligned(font, text, x, alignment);
  int32_t bounds[4] = {x, y, x, y};
  for (uint32_t i = 0; text[i]; ++i)
    if (text[i] >= 32) {
      uint32_t entry = glyph(font, character(font, text[i]));
      if (BB_U32(entry + 4))
        include(entry, x, y, bounds);
      x = wrap((uint32_t)x + advance(font, entry));
    }
  BB_I32(left) = bounds[0];
  BB_I32(top) = bounds[1];
  BB_I32(right) = bounds[2];
  BB_I32(bottom) = bounds[3];
  return (uint32_t)bounds[2];
}

/* 0x00430bb0..0x00430ef8: separate scratch bank, line height captured once. */
uint32_t BB_THISCALL bb_font_bounds_wrapped(void *object, const char *string,
                                            int32_t x, int32_t y, int32_t limit,
                                            uint32_t alignment, int32_t *left,
                                            int32_t *top, int32_t *right,
                                            int32_t *bottom) {
  uint32_t font = bb_address_encode(object), bank = 0x006b6320;
  uint32_t count = lines(font, BB_DATA(string), limit, bank);
  uint32_t height = BB_U32(font + 0x1c0c) * 2;
  int32_t cursor = x, bounds[4] = {x, y, x, y};
  for (uint32_t line = 0; line < count; ++line) {
    cursor =
        line_x(x, limit, BB_I32(bank + 0x4100 + line * 4), alignment, cursor);
    if (!line)
      bounds[0] = bounds[2] = cursor;
    unsigned char *text = BB_DATA(bank + 0x100 + line * 256);
    for (uint32_t i = 0; text[i]; ++i) {
      uint32_t entry = glyph(font, character(font, text[i]));
      if (BB_U32(entry + 4))
        include(entry, cursor, y, bounds);
      cursor = wrap((uint32_t)cursor + advance(font, entry));
    }
    y = wrap((uint32_t)y + height);
  }
  BB_I32(left) = bounds[0];
  BB_I32(top) = bounds[1];
  BB_I32(right) = bounds[2];
  BB_I32(bottom) = bounds[3];
  return bb_address_encode(right);
}

#ifndef BB_MODEL_RUNTIME
#ifdef _WIN32
#define BB_FONT_CONSTRUCT "_bb_font_construct"
#define BB_FONT_DESTROY "_bb_font_destroy"
#else
#define BB_FONT_CONSTRUCT "bb_font_construct"
#define BB_FONT_DESTROY "bb_font_destroy"
#endif
void __attribute__((naked)) bb_font_construct_entry(void) {
  __asm__ volatile("pushl %ecx\n\t"
                   "pushl %ecx\n\t"
                   "call " BB_FONT_CONSTRUCT "\n\t"
                   "addl $4, %esp\n\t"
                   "popl %ecx\n\t"
                   "leal 0x1c04(%ecx), %edx\n\t"
                   "ret");
}
void __attribute__((naked)) bb_font_destroy_entry(void) {
  __asm__ volatile("cmpl $0, (%ecx)\n\t"
                   "je 1f\n\t"
                   "pushl %ecx\n\t"
                   "call " BB_FONT_DESTROY "\n\t"
                   "addl $4, %esp\n"
                   "1: ret");
}
/* 0x00430550..0x0043055c: preserve ECX/EDX and return the spacing in EAX. */
void __attribute__((naked)) bb_font_spacing_entry(void) {
  __asm__ volatile("movl 4(%esp), %eax\n\t"
                   "movl %eax, 0x1c04(%ecx)\n\t"
                   "ret $4");
}

uint32_t BB_THISCALL bb_font_load(void *object, void *owner, const char *name) {
  uint32_t scratch[BB_FONT_LOAD_BYTES / 4];
  return bb_font_load_c(object, owner, name, bb_address_encode(scratch));
}
#endif
