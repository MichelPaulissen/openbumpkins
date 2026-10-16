/* SPDX-License-Identifier: GPL-3.0-only */
#include "movie_color.h"
#include "abi.h"
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include "movie_calls.h"
#endif
#include "movie_runtime.h"
typedef uint32_t(BB_STDCALL *query_fn)(void *, void *);
static void *method(void *object, uint32_t offset) {
  return BB_POINTER((uintptr_t)BB_POINTER((uintptr_t)object) + offset);
}
static uint32_t shl(uint32_t value, uint32_t shift) {
  return value << (shift & 31);
}
static uint32_t sar(uint32_t value, uint32_t shift) {
  return (uint32_t)((int32_t)value >> (shift & 31));
}
static uint32_t fixed_round(uint32_t value, uint32_t coefficient) {
  int64_t product = (int64_t)(int32_t)value * (int32_t)coefficient;
  return (uint32_t)((uint64_t)product >> 16) + ((uint64_t)product >> 15 & 1);
}
static uint32_t rounded_channel(uint32_t value, uint32_t bits, uint32_t *raw) {
  int32_t divisor = (int32_t)shl(1, 8 - bits);
  *raw = (uint32_t)((int32_t)value / divisor);
  uint32_t bias = shl(1, 7 - bits);
  uint32_t adjusted = (int32_t)value < 0 ? value - bias : value + bias;
  uint32_t rounded = (uint32_t)((int32_t)adjusted / divisor);
  uint32_t limit = (int32_t)value < 0 ? 1 - shl(1, bits) : shl(1, bits) - 1;
  if ((int32_t)value < 0 ? (int32_t)rounded < (int32_t)limit
                        : (int32_t)rounded > (int32_t)limit)
    rounded = limit;
  return rounded;
}
/* 0x45cca0: three channels plus an unused alpha stack slot, precision and
 * shifts. Borrow correction propagates across the two packed color words. */
uint32_t BB_FASTCALL __attribute__((noinline))
bb_movie_color_pack_entry(uint32_t red, uint32_t green, uint32_t blue,
                          uint32_t alpha __attribute__((unused)), uint32_t rb,
                          uint32_t gb, uint32_t bb, uint32_t rs, uint32_t gs,
                          uint32_t bs) {
  uint32_t r, g, b;
  uint32_t rr = rounded_channel(red, rb, &r);
  uint32_t gr = rounded_channel(green, gb, &g);
  uint32_t br = rounded_channel(blue, bb, &b);
  if ((int32_t)br < 0)
    --gr;
  if ((int32_t)gr < 0)
    --rr;
  if ((int32_t)rr < 0)
    --b;
  if ((int32_t)b < 0)
    --g;
  if ((int32_t)g < 0)
    --r;
  uint32_t rm = shl(1, rb) - 1, gm = shl(1, gb) - 1, bm = shl(1, bb) - 1;
  uint32_t result = shl(r & rm, rs + 16) | shl(g & gm, gs + 16) |
                    shl(b & bm, bs + 16) | shl(rr & rm, rs) | shl(gr & gm, gs) |
                    shl(br & bm, bs);
  if (rb + gb + bb != 16)
    result |= 0x80008000;
  return result;
}
/* 0x45ced0: unsigned byte lanes preserve cross-channel signed carries. */
void BB_THISCALL __attribute__((noinline))
bb_movie_color_rgba_entry(uint32_t alpha) {
  for (uint32_t i = 0; i < 128; ++i) {
    uint32_t q = (int32_t)(int8_t)(i * 2 + 1);
    uint32_t y = q + 128;
    BB_U32(0x6cc25c + i * 4) = (alpha << 24) | (y << 16) | (y << 8) | y;
  }
  for (uint32_t i = 0; i < 128; ++i) {
    uint32_t q = (int32_t)(int8_t)(i * 2 + 1);
    uint32_t g = fixed_round(q, 0xffffa7ea), b = fixed_round(q, 0x1c5a1);
    if ((int32_t)b < 0)
      --g;
    BB_U32(0x6cc45c + i * 4) =
        ((int32_t)g < 0 ? 0xffff0000 : 0) | ((g & 255) << 8) | (b & 255);
  }
  for (uint32_t i = 0; i < 128; ++i) {
    uint32_t q = (int32_t)(int8_t)(i * 2 + 1);
    uint32_t r = fixed_round(q, 0x166e9), g = fixed_round(q, 0xffff4931);
    if ((int32_t)g < 0)
      --r;
    BB_U32(0x6cc65c + i * 4) = ((int32_t)r < 0 ? 0xff000000 : 0) |
                               ((r & 255) << 16) | ((g & 255) << 8);
  }
}
static uint32_t float_round(long double value) {
  long double zero = BB_FLOAT(0x482340);
  value -= (long double)BB_DOUBLE(value < zero ? 0x482348 : 0x482330);
  return (uint32_t)(int64_t)value;
}
static void channel_table(uintptr_t out, int32_t first, int32_t end,
                          uint32_t bits, uint32_t shift) {
  uint32_t mask = shl(1, bits) - 1, bias = shl(1, 7 - bits);
  for (int32_t n = first; n < end; n += 2, out += 4) {
    uint32_t value = n < 0 ? 0 : n > 255 ? 255 : (uint32_t)n;
    uint32_t raw = sar(value, 8 - bits), rounded = sar(value + bias, 8 - bits);
    if ((int32_t)rounded > (int32_t)mask)
      rounded = mask;
    BB_U32(out) = shl(raw, shift + 16) + shl(rounded, shift);
  }
}
/* 0x45c9b0: YUV lane offsets, channel quantization and saturation lookup. */
void BB_FASTCALL __attribute__((noinline))
bb_movie_color_tables_entry(uint32_t rb, uint32_t gb, uint32_t bb, uint32_t rs,
                            uint32_t gs, uint32_t bs) {
  uint32_t packed = 0x2c821871;
  for (uint32_t i = 0; i < 128; ++i) {
    long double v =
        (long double)((int32_t)i - 64) - (long double)BB_DOUBLE(0x482330);
    BB_U32(0x6be9d0 + i * 4) = packed;
    uint32_t g = float_round(v * (long double)BB_DOUBLE(0x482338));
    uint32_t b = float_round(v * (long double)BB_DOUBLE(0x482350));
    BB_U32(0x6bebd0 + i * 4) = ((g & 511) << 11) + (b & 511);
    uint32_t r = float_round(v * (long double)BB_DOUBLE(0x482358));
    g = float_round(v * (long double)BB_DOUBLE(0x482360));
    BB_U32(0x6bedd0 + i * 4) = (((r & 511) << 12) + (g & 511)) << 11;
    packed += 0x800801;
  }
  channel_table(0x6befd0, -177, 435, rb, rs);
  channel_table(0x6bf498, -133, 391, gb, gs);
  channel_table(0x6bf8b0, -225, 483, bb, bs);
  for (uint32_t i = 0; i < 1024; ++i) {
    int32_t value = (int32_t)i * 2 - 1024;
    BB_U32(0x6bfe38 + i * 4) = value < -128  ? (uint32_t)-128
                               : value > 127 ? 127
                                             : (uint32_t)value;
  }
}
static uint32_t count_bits(uint32_t mask) {
  uint32_t n = 0;
  for (unsigned i = 0; i < 31; ++i)
    if (mask & (1u << i))
      ++n;
  return n;
}
/* 0x45c380: four 8x8 luma quadrants sharing chroma; pitch is rounded for row
 * increments but the lower quadrant's origin uses the unrounded pitch. */
void BB_FASTCALL __attribute__((noinline))
bb_movie_color_block_entry(uint32_t y0, uint32_t y1, uint32_t y2, uint32_t y3,
                           uint32_t u, uint32_t v, uint32_t *output,
                           uint32_t pitch) {
  uint32_t ui = sar(fixed_round((int32_t)(int8_t)u, BB_U32(0x6cc15c)), 1) & 127;
  uint32_t vi = sar(fixed_round((int32_t)(int8_t)v, BB_U32(0x6cc15c)), 1) & 127;
  uint32_t chroma = BB_U32(0x6cc45c + ui * 4) + BB_U32(0x6cc65c + vi * 4);
  uint32_t luma[4] = {y0, y1, y2, y3};
  for (unsigned q = 0; q < 4; ++q) {
    uint32_t index =
        sar(fixed_round((int32_t)(int8_t)luma[q], BB_U32(0x6cc15c)), 1) & 127;
    uint32_t value = BB_U32(0x6cc25c + index * 4) + chroma;
    uint32_t row =
        bb_address_encode(output) + (q & 1) * 32 + (q & 2 ? pitch * 8 : 0);
    for (unsigned y = 0; y < 8; ++y, row += pitch & ~3u)
      for (unsigned x = 0; x < 8; ++x)
        BB_U32(row + x * 4) = value;
  }
}
static uint32_t trailing_bits(uint32_t mask) {
  uint32_t n = 0;
  while (!(mask & 1)) {
    mask = sar(mask, 1);
    ++n;
  }
  return n;
}
/* 0x45c560: arena creation, live surface queries and complete lookup setup. */
void BB_THISCALL __attribute__((noinline))
bb_movie_frame_setup_entry(void *object) {
  uint32_t p = bb_address_encode(object);
#ifdef BB_MODEL_RUNTIME
  BB_U32(p + 0x39c) = MV2(0x46daa0, 1, 0x5a000);
  BB_U32(p + 0x3a0) = MV2(0x46daa0, 1, 0x5a000);
#else
  typedef void *(*calloc_fn)(uint32_t, uint32_t);
  calloc_fn allocate = (calloc_fn)BB_CODE(0x46daa0);
  BB_U32(p + 0x39c) = (uintptr_t)allocate(1, 0x5a000);
  BB_U32(p + 0x3a0) = (uintptr_t)allocate(1, 0x5a000);
#endif
#ifdef BB_MODEL_RUNTIME
  MOVIE_LOCAL(bb_memory_u32, desc, 27, 108);
#else
  uint32_t desc[27] = {108};
#endif
  void *surface = BB_POINTER(p + 0x408);
#ifdef BB_MODEL_RUNTIME
  if (!MV2(MV_WORD(method(surface, 0x58)), surface, desc)) {
#else
  if (!((query_fn)BB_CODE(method(surface, 0x58)))(surface, desc)) {
#endif
    BB_U32(0x6be1ac) = desc[3];
    BB_U32(0x6be1a8) = desc[2];
  }
  surface = BB_POINTER(p + 0x408);
  if (surface) {
#ifdef BB_MODEL_RUNTIME
    MOVIE_LOCAL(bb_memory_u32, format, 8, 32);
#else
    uint32_t format[8] = {32};
#endif
#ifdef BB_MODEL_RUNTIME
    MV2(MV_WORD(method(surface, 0x54)), surface, format);
#else
    ((query_fn)BB_CODE(method(surface, 0x54)))(surface, format);
#endif
    BB_U32(p + 0x3e0) = count_bits(format[4]);
    BB_U32(p + 0x3e4) = count_bits(format[5]);
    uint32_t blue = bb_movie_bits_entry(format[6]);
    BB_U32(p + 0x3e8) = blue;
    BB_U32(p + 0x3ec) = trailing_bits(format[4]);
    BB_U32(p + 0x3f0) = trailing_bits(format[5]);
    BB_U32(p + 0x3f4) = trailing_bits(format[6]);
    BB_U32(p + 0x3fc) = BB_U32(p + 0x3e0) + BB_U32(p + 0x3e4) + blue;
    BB_U32(p + 0x3f8) = format[4] | format[5] | format[6];
  }
  uint32_t packed_mode = BB_U32(p + 0x3fc) == 16 || BB_U32(p + 0x3fc) == 15;
  if (!packed_mode)
    bb_movie_color_rgba_entry(255);
  else {
    uint32_t rb = BB_U32(p + 0x3e0), gb = BB_U32(p + 0x3e4),
             bb = BB_U32(p + 0x3e8);
    uint32_t rs = BB_U32(p + 0x3ec), gs = BB_U32(p + 0x3f0),
             bs = BB_U32(p + 0x3f4);
    for (uint32_t i = 0; i < 128; ++i) {
      uint32_t q = (int32_t)(int8_t)(i * 2 + 1) + 128;
      BB_U32(0x6cc25c + i * 4) =
          bb_movie_color_pack_entry(q, q, q, 255, rb, gb, bb, rs, gs, bs);
    }
    for (uint32_t i = 0; i < 128; ++i) {
      uint32_t q = (int32_t)(int8_t)(i * 2 + 1);
      BB_U32(0x6cc45c + i * 4) = bb_movie_color_pack_entry(
          0, fixed_round(q, 0xffffa7ea), fixed_round(q, 0x1c5a1), 0, rb, gb, bb,
          rs, gs, bs);
    }
    for (uint32_t i = 0; i < 128; ++i) {
      uint32_t q = (int32_t)(int8_t)(i * 2 + 1);
      BB_U32(0x6cc65c + i * 4) = bb_movie_color_pack_entry(
          fixed_round(q, 0x166e9), fixed_round(q, 0xffff4931), 0, 0, rb, gb, bb,
          rs, gs, bs);
    }
  }
  for (uint32_t i = 0; i < 1024; ++i) {
    uint32_t value = i < 384 ? 0 : i > 639 ? 255 : i - 384;
    if (!packed_mode) {
      BB_U32(0x6bb1a8 + i * 4) = value;
      BB_U32(0x6bd1a8 + i * 4) = value << 16;
      BB_U32(0x6bc1a8 + i * 4) = value << 8;
    } else {
      uint32_t red = shl(sar(value, 8 - BB_U32(p + 0x3e0)), BB_U32(p + 0x3ec));
      uint32_t green =
          shl(sar(value, 8 - BB_U32(p + 0x3e4)), BB_U32(p + 0x3f0));
      uint32_t blue = shl(sar(value, 8 - BB_U32(p + 0x3e8)), BB_U32(p + 0x3f4));
      BB_U32(0x6bd1a8 + i * 4) = (red << 16) | red;
      BB_U32(0x6bc1a8 + i * 4) = (green << 16) | green;
      BB_U32(0x6bb1a8 + i * 4) = (blue << 16) | blue;
    }
  }
  if (packed_mode)
    bb_movie_color_tables_entry(BB_U32(p + 0x3e0), BB_U32(p + 0x3e4),
                                BB_U32(p + 0x3e8), BB_U32(p + 0x3ec),
                                BB_U32(p + 0x3f0), BB_U32(p + 0x3f4));
  BB_U32(p + 0x3a8) = 1;
}
