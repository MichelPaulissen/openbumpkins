/* SPDX-License-Identifier: GPL-3.0-only */
#include "movie_iqt.h"
#include "crt_calculation.h"
#include "crt_math_errors.h"
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include "movie_calls.h"
#endif
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif
static uint32_t shl(uint32_t v, uint32_t n) { return v << (n & 31); }
static uint32_t shr(uint32_t v, uint32_t n) { return v >> (n & 31); }
static uint32_t sar(uint32_t v, uint32_t n) {
  return (uint32_t)((int32_t)v >> (n & 31));
}
static uint32_t ror(uint32_t v, uint32_t n) {
  n &= 31;
  return n ? (v >> n) | (v << (32 - n)) : v;
}
static unsigned parity(uint32_t v) {
  v ^= v >> 4;
  return (0x9669 >> (v & 15)) & 1;
}
static uint32_t flags(uint32_t f, uint32_t a, uint32_t b, unsigned add) {
  uint32_t v = add ? a + b : a - b;
  uint32_t overflow = add ? ~(a ^ b) & (a ^ v) : (a ^ b) & (a ^ v);
  return (f & ~0x8d5u) | (add ? v < a : a < b) | (parity(v) << 2) |
         ((a ^ b ^ v) & 16) | ((v == 0) << 6) | ((v >> 31) << 7) |
         ((overflow >> 31) << 11);
}
static uint32_t arguments(const uint32_t *r) { return r[3] + 8; }
#ifndef BB_MODEL_RUNTIME
#define IQT_ENTRY(type, name, body, parameters)                                \
  type __attribute__((naked, noinline)) name parameters {                      \
    __asm__ volatile("pushfl;pushal;cld;pushl %esp;call " BB_ASM(              \
        body) ";addl $4,%esp;popal;popfl;ret");                                \
  }
#else
#define IQT_ENTRY(...)
#endif
/* 0x46297a: read backwards and write backwards; each load is an unaligned
 * DWORD shifted logically, followed by the original sign extension. */
void BB_IQT_F bb_movie_iqt_signed_entry(void *output, const void *input,
                                        uint32_t count, uint32_t bits) {
  uint32_t offset = (count - 1) * bits;
  uint32_t out = bb_address_encode(output) + count * 4;
  uint32_t sign = shl(1, bits - 1), extension = shl(UINT32_MAX, bits);
  for (uint32_t i = 0; (int32_t)i < (int32_t)count; ++i) {
    uint32_t value =
        shr(BB_U32(bb_address_encode(input) + sar(offset, 3)), offset & 7) &
        (shl(1, bits) - 1);
    offset -= bits;
    if (value & sign)
      value |= extension;
    out -= 4;
    BB_U32(out) = value;
  }
}
/* 0x462a2f: snapshot selectors but re-read the four palette bytes per store. */
void BB_IQT_F bb_movie_iqt_four_color_entry(const void *input, void *output,
                                            uint32_t count) {
  uint32_t in = bb_address_encode(input), out = bb_address_encode(output);
  for (uint32_t i = 0; (int32_t)i < (int32_t)count; ++i, in += 8, out += 16) {
    uint32_t code = BB_U32(in + 4);
    for (unsigned j = 0; j < 16; ++j, code >>= 2)
      BYTE(out + 15 - j) = BYTE(in + (code & 3));
  }
}
/* 0x4632a0: literal/match compression with forward REP overlap semantics. */
void __attribute__((used, noinline)) bb_movie_iqt_lz_c(uint32_t *r) {
  uint32_t args = arguments(r), in = BB_U32(args), out = BB_U32(args + 4);
  if (!in) {
    r[8] = (r[8] & ~0x8d5u) | 0x44;
    return;
  }
  unsigned optional = WORD(in) & 1;
  in += 2 + (optional ? 3 : 0);
  (void)BYTE(in);
  (void)BYTE(in + 1);
  (void)BYTE(in + 2);
  in += 3;
  if (!BB_U32(args + 8)) {
    r[8] = (r[8] & ~0x8d5u) | 0x44;
    return;
  }
  uint32_t step = r[8] & 0x400 ? UINT32_MAX : 1;
  for (;;) {
    uint32_t code = BB_U32(in), command = code & 255;
    uint32_t source;
    uint32_t literals, count, distance;
    if (command < 128) {
      literals = command & 3;
      source = in + 2;
      while (literals--) {
        BYTE(out) = BYTE(source);
        out += step;
        source += step;
      }
      in = source;
      distance = ((command >> 5) << 8) | ((code >> 8) & 255);
      count = ((command & 28) >> 2) + 3;
    } else if (command < 192) {
      literals = (code >> 14) & 3;
      source = in + 3;
      while (literals--) {
        BYTE(out) = BYTE(source);
        out += step;
        source += step;
      }
      in = source;
      distance = ((code >> 16) & 255) | (((code >> 8) & 63) << 8);
      count = (command & 63) + 4;
    } else if (command < 224) {
      literals = command & 3;
      source = in + 4;
      while (literals--) {
        BYTE(out) = BYTE(source);
        out += step;
        source += step;
      }
      in = source;
      distance = ((command & 16) << 12) | ((code >> 16) & 255) |
                 (((code >> 8) & 255) << 8);
      count = ((code >> 24) | ((command >> 2) << 8)) & 1023;
      count += 5;
    } else if (command < 252) {
      source = in + 1;
      count = (command & 31) + 1;
      while (count--) {
        BB_U32(out) = BB_U32(source);
        out += step * 4;
        source += step * 4;
      }
      in = source;
      continue;
    } else {
      count = command & 3;
      source = in + 1;
      while (count--) {
        BYTE(out) = BYTE(source);
        out += step;
        source += step;
      }
      uint32_t result = command - 252;
      r[8] = (r[8] & ~0x8d5u) | (parity(result) << 2) | ((result == 0) << 6);
      return;
    }
    source = out - distance - 1;
    while (count--) {
      BYTE(out) = BYTE(source);
      out += step;
      source += step;
    }
  }
}
IQT_ENTRY(void, bb_movie_iqt_lz_entry, bb_movie_iqt_lz_c,
          (const void *a __attribute__((unused)),
           void *b __attribute__((unused)), uint32_t c __attribute__((unused))))
/* 0x4633c3: two extracted indices share one loaded/rotated DWORD. */
void __attribute__((used, noinline)) bb_movie_iqt_indices_c(uint32_t *r) {
  uint32_t args = arguments(r), out = BB_U32(args), in = BB_U32(args + 4);
  uint32_t remaining = BB_U32(args + 8), bits = BB_U32(args + 12);
  uint32_t offset = 0, mask = shl(1, bits) - 1, ecx = bits;
  remaining -= 4;
  while ((int32_t)remaining >= 0) {
    uint32_t value = ror(BB_U32(in + (offset >> 3)), offset & 7);
    BB_U32(out) = value & mask;
    value = ror(value, bits);
    BB_U32(out + 4) = value & mask;
    uint32_t second = offset + bits * 2;
    value = ror(BB_U32(in + (second >> 3)), second & 7);
    BB_U32(out + 8) = value & mask;
    value = ror(value, bits);
    BB_U32(out + 12) = value & mask;
    out += 16;
    remaining -= 4;
    offset += bits * 4;
  }
  uint32_t previous = remaining;
  remaining += 3;
  r[8] = flags(r[8], previous, 3, 1);
  while ((int32_t)remaining >= 0) {
    ecx = offset & 7;
    BB_U32(out) = ror(BB_U32(in + (offset >> 3)), ecx) & mask;
    offset += bits;
    r[8] = flags(r[8], remaining, 1, 0);
    --remaining;
    out += 4;
  }
  r[4] = offset;
  r[5] = remaining;
  r[6] = ecx;
  r[7] = bits;
}
IQT_ENTRY(uint32_t, bb_movie_iqt_indices_entry, bb_movie_iqt_indices_c,
          (void *a __attribute__((unused)),
           const void *b __attribute__((unused)),
           uint32_t c __attribute__((unused)),
           uint32_t d __attribute__((unused))))
/* Original binary callers accept EBX as an additional result. Ordinary C
 * callers use the same worker through a standard callee-saved boundary. */
static void indices(void *output, const void *input, uint32_t count,
                    uint32_t bits) {
#ifdef BB_MODEL_RUNTIME
  MV4(0x4633c3, output, input, count, bits);
#else
  uint32_t args[4] = {bb_address_encode(output), bb_address_encode(input),
                      count, bits};
  uint32_t state[9] = {0};
  state[3] = (uintptr_t)args - 8;
  bb_movie_iqt_indices_c(state);
#endif
}
/* 0x463464: four-row motion/literal cells, including mandatory first cells
 * for zero counts and the original mutation of both stack count slots. */
void __attribute__((used, noinline)) bb_movie_iqt_blocks_c(uint32_t *r) {
  uint32_t args = arguments(r), indices = BB_U32(args),
           output = BB_U32(args + 8);
  uint32_t initial = BB_U32(args + 28), reuse = BB_U32(args + 12);
  uint32_t literals = BB_U32(args + 20) - reuse * 16;
  uint32_t displacement = BB_U32(args + 4) - output;
  uint32_t stride = BB_U32(args + 24), stride3 = stride * 3;
  do {
    BB_U32(args + 28) = initial;
    do {
      uint32_t index = BB_U32(indices);
      indices += 4;
      uint32_t source;
      if ((int32_t)index < (int32_t)reuse) {
        source = BB_U32(BB_U32(args + 16) + index * 4) + displacement + output;
        for (unsigned row = 0; row < 4; ++row)
          BB_U32(output + row * stride) = BB_U32(source + row * stride);
      } else {
        source = literals + index * 16;
        for (unsigned row = 0; row < 4; ++row)
          BB_U32(output + row * stride) = BB_U32(source + row * 4);
      }
      output += 4;
      BB_U32(args + 28) -= 1;
    } while ((int32_t)BB_U32(args + 28) > 0);
    output += stride3;
    uint32_t previous = BB_U32(args + 32);
    BB_U32(args + 32) = previous - 1;
    r[8] = flags(r[8], previous, 1, 0);
  } while ((int32_t)BB_U32(args + 32) > 0);
}
IQT_ENTRY(
    void, bb_movie_iqt_blocks_entry, bb_movie_iqt_blocks_c,
    (const void *a __attribute__((unused)),
     const void *b __attribute__((unused)), void *c __attribute__((unused)),
     uint32_t d __attribute__((unused)), const void *e __attribute__((unused)),
     const void *f __attribute__((unused)), uint32_t g __attribute__((unused)),
     uint32_t h __attribute__((unused)), uint32_t i __attribute__((unused))))

/* 0x463518: palette-byte lookup and rotation, retaining forward aliasing. */
uint32_t __attribute__((noinline))
bb_movie_iqt_rotate_entry(void *output, const void *table, uint32_t count) {
  uint32_t out = bb_address_encode(output), palette = bb_address_encode(table);
  uint32_t value = 0;
  for (uint32_t i = 0; (int32_t)i < (int32_t)count; ++i)
    for (unsigned channel = 0; channel < 3; ++channel, ++out) {
      value = BYTE(palette + BYTE(out) * 4);
      value = (value >> 2) | ((value << 6) & 255);
      BYTE(out) = value;
    }
  return value;
}
/* 0x462f8a: typed C math calls retain double arguments and the extended
 * intermediate before integral floor. */
void BB_IQT_M bb_movie_iqt_gamma_entry(uint32_t gamma) {
  if ((int32_t)gamma <= 0)
    gamma = 1;
  volatile double exponent = (long double)BB_DOUBLE(0x482378) / (int32_t)gamma;
  union encoded_double {
    double value;
    uint32_t words[2];
  };
  double base = 0;
  for (unsigned i = 0; i < 256; ++i) {
    union encoded_double input = {.value = base}, power = {.value = exponent};
    volatile long double value = bb_crt_calculation_power_entry(
        input.words[0], input.words[1], power.words[0], power.words[1]);
    value = value * (long double)BB_DOUBLE(0x482380);
    value = value + (long double)BB_DOUBLE(0x482388);
    volatile double rounded = value;
    union encoded_double floor = {.value = rounded};
    value = bb_crt_math_floor_entry(floor.words[0], floor.words[1]);
    uint32_t integer = (uint32_t)(int64_t)value;
    BYTE(0x6c0e38 + i) = (int32_t)integer > 253 ? 253 : integer;
    base += 0.00390625;
  }
}
/* 0x463060: precisions are captured, while shifts/mode/palette stay live. */
void BB_IQT_M bb_movie_iqt_palette_entry(void *object) {
  uint32_t p = bb_address_encode(object);
  uint32_t red = 8 - BB_U32(p + 0x3e0), green = 8 - BB_U32(p + 0x3e4),
           blue = 8 - BB_U32(p + 0x3e8);
  for (unsigned i = 0; i < 256; ++i) {
    uint32_t value = shl(shr(BYTE(p + 0x4c8 + i * 3), red), BB_U32(p + 0x3ec));
    value |= shl(shr(BYTE(p + 0x4c9 + i * 3), green), BB_U32(p + 0x3f0));
    value |= shl(shr(BYTE(p + 0x4ca + i * 3), blue), BB_U32(p + 0x3f4));
    if (BB_U32(p + 0x3fc) == 16 || BB_U32(p + 0x3fc) == 15)
      value |= value << 16;
    BB_U32(0x6c0f38 + i * 4) = value;
  }
}
/* 0x463174: every partial-width row still copies eight indices at a time. */
void BB_IQT_M bb_movie_iqt_expand_entry(void *object) {
  uint32_t p = bb_address_encode(object), in = BB_U32(p + 0x4ac),
           out = BB_U32(p + 0x39c);
  for (uint32_t row = 0; row < BB_U32(p + 4); ++row)
    for (uint32_t x = 0; x < BB_U32(p + 8); x += 8, in += 8, out += 32)
      for (unsigned i = 0; i < 8; ++i)
        BB_U32(out + i * 4) = BB_U32(0x6c0f38 + BYTE(in + i) * 4);
}
static void free_memory(uint32_t p) {
#ifdef BB_MODEL_RUNTIME
  MV1(0x46e110, p);
#else
  ((void (*)(void *))BB_CODE(0x46e110))((void *)(uintptr_t)p);
#endif
}
static uint32_t allocate(uint32_t bytes) {
#ifdef BB_MODEL_RUNTIME
  return MV1(0x46d660, bytes);
#else
  return (uintptr_t)((void *(*)(uint32_t))BB_CODE(0x46d660))(bytes);
#endif
}
/* 0x462c25: palette copy, old-buffer release, four allocations and row map.
 * Allocation failures preserve the original partial state and false return. */
uint32_t BB_IQT_F bb_movie_iqt_init_entry(void *object, const void *input) {
  uint32_t p = bb_address_encode(object), in = bb_address_encode(input);
  BB_U32(p + 0x484) = BB_U32(in - 2) >> 16;
  uint32_t width = BB_U32(p + 0x484);
  BB_U32(p + 0x488) = BB_U32(in) >> 16;
  uint32_t height = BB_U32(p + 0x488);
  BB_U32(p + 0x48c) = BB_U32(in + 2) >> 16;
  BB_U32(p + 0x490) = BB_U32(in + 4) >> 16;
  uint32_t colors = BB_U32(p + 0x490);
  for (uint32_t i = 0; (int32_t)i < (int32_t)colors; ++i)
    for (unsigned channel = 0; channel < 3; ++channel)
      BYTE(p + 0x4c8 + i * 3 + channel) = BYTE(in + 12 + i * 3 + channel);
  uint32_t unaligned = width * height + 3, bytes = unaligned & ~3u;
  uint32_t fields[] = {0x4ac, 0x4b0, 0x4b8, 0x4b4};
  for (unsigned i = 0; i < 4; ++i)
    if (BB_U32(p + fields[i])) {
      free_memory(BB_U32(p + fields[i]));
      BB_U32(p + fields[i]) = 0;
    }
  BB_U32(p + 0x4ac) = allocate(bytes);
  BB_U32(p + 0x4b0) = allocate(bytes);
  BB_U32(p + 0x4b8) = allocate(height * 8);
  uint32_t rounding = (unaligned >> 31) ? 15 : 0;
  BB_U32(p + 0x4b4) = allocate(sar(bytes + rounding, 4) << 2);
  if (!BB_U32(p + 0x4ac) || !BB_U32(p + 0x4b0) || !BB_U32(p + 0x4b8) ||
      !BB_U32(p + 0x4b4))
    return 0;
  BB_U32(p + 8) = width;
  BB_U32(p + 4) = height;
  uint32_t offset = 0 - height * width;
  for (uint32_t i = 0; (int32_t)i < (int32_t)(height * 2); ++i, offset += width)
    BB_U32(BB_U32(p + 0x4b8) + i * 4) = offset;
  bb_movie_iqt_lz_entry((const void *)(uintptr_t)(in + colors * 3 + 12),
                        BB_POINTER(p + 0x4ac), 1);
  return 1;
}
/* 0x4624d0: swap old/new frames, grow motion/literal storage and apply delta.
 */
uint32_t BB_IQT_F bb_movie_iqt_delta_entry(void *object, const void *input) {
  uint32_t p = bb_address_encode(object), in = bb_address_encode(input);
  uint32_t result = 1, width = BB_U32(p + 0x484), height = BB_U32(p + 0x488);
  uint32_t previous = BB_U32(p + 0x4b0);
  BB_U32(p + 0x4b0) = BB_U32(p + 0x4ac);
  BB_U32(p + 0x4ac) = previous;
  uint32_t fields[] = {0x494, 0x498, 0x49c, 0x4a0}, sizes[4];
  for (unsigned i = 0; i < 4; ++i) {
    BB_U32(p + fields[i]) = BB_U32(in - 2 + i * 2) >> 16;
    sizes[i] = BB_U32(p + fields[i]);
  }
  uint32_t motions = sizes[0], raw = sizes[1], packed = sizes[2],
           bits = sizes[3];
  if (motions) {
    if ((int32_t)BB_U32(p + 0x4a4) < (int32_t)motions) {
      if (BB_U32(p + 0x4c0)) {
        free_memory(BB_U32(p + 0x4c0));
        BB_U32(p + 0x4c0) = 0;
        BB_U32(p + 0x4a4) = 0;
      }
      if (BB_U32(p + 0x4c4)) {
        free_memory(BB_U32(p + 0x4c4));
        BB_U32(p + 0x4c4) = 0;
      }
    }
    if (!BB_U32(p + 0x4c0)) {
      BB_U32(p + 0x4c0) = allocate(motions * 8);
      BB_U32(p + 0x4a4) = motions;
    }
    if (!BB_U32(p + 0x4c4)) {
      BB_U32(p + 0x4c4) = allocate(motions * 4);
      BB_U32(p + 0x4a4) = motions;
    }
    if (!BB_U32(p + 0x4c0) || !BB_U32(p + 0x4c4))
      result = 0;
  }
  uint32_t cells = raw + packed;
  if (cells) {
    if ((int32_t)BB_U32(p + 0x4a8) < (int32_t)cells && BB_U32(p + 0x4bc)) {
      free_memory(BB_U32(p + 0x4bc));
      BB_U32(p + 0x4bc) = 0;
    }
    if (!BB_U32(p + 0x4bc)) {
      BB_U32(p + 0x4bc) = allocate(cells * 16);
      BB_U32(p + 0x4a8) = cells;
    }
    if (!BB_U32(p + 0x4bc))
      result = 0;
  }
  if (!result)
    return 0;
  uint32_t motion_bytes = sar((motions * 20 + 31) & ~31u, 3);
  uint32_t data = in + 12;
  bb_movie_iqt_signed_entry(BB_POINTER(p + 0x4c0),
                            (const void *)(uintptr_t)data, motions * 2, 10);
  uint32_t rows = BB_U32(p + 0x4b8) + height * 4;
  for (uint32_t i = 0; (int32_t)i < (int32_t)motions; ++i) {
    uint32_t pairs = BB_U32(p + 0x4c0);
    uint32_t offset = BB_U32(rows + BB_U32(pairs + i * 8 + 4) * 4);
    offset += BB_U32(BB_U32(p + 0x4c0) + i * 8);
    BB_U32(BB_U32(p + 0x4c4) + i * 4) = offset;
  }
  data += motion_bytes;
  uint32_t bytes = raw * 16;
  if (bytes)
#ifdef BB_MODEL_RUNTIME
    MV3(0x46f1d0, BB_U32(p + 0x4bc), data, bytes);
#else
    ((void *(*)(void *, const void *, uint32_t))BB_CODE(0x46f1d0))(
        BB_POINTER(p + 0x4bc), (const void *)(uintptr_t)data, bytes);
#endif
  data += bytes;
  if (packed * 8 != 0)
    bb_movie_iqt_four_color_entry(
        (const void *)(uintptr_t)data,
        (void *)(uintptr_t)(BB_U32(p + 0x4bc) + raw * 16), packed);
  data += packed * 8;
  uint32_t count = sar(width * height, 4);
  bytes = sar((count * bits + 31) & ~31u, 3);
  if (bytes)
    indices(BB_POINTER(p + 0x4b4), (const void *)(uintptr_t)data, count, bits);
  bb_movie_iqt_blocks_entry(
      BB_POINTER(p + 0x4b4), BB_POINTER(p + 0x4b0), BB_POINTER(p + 0x4ac),
      BB_U32(p + 0x494), BB_POINTER(p + 0x4c4), BB_POINTER(p + 0x4bc),
      BB_U32(p + 0x484), sar(BB_U32(p + 0x484), 2), sar(BB_U32(p + 0x488), 2));
  return result;
}

#ifdef BB_MODEL_RUNTIME
void bb_movie_iqt_lz_entry(const void *in, void *out, uint32_t n) {
  MV3(0x4632a0, in, out, n);
}
uint32_t bb_movie_iqt_indices_entry(void *out, const void *in, uint32_t count,
                                    uint32_t bits) {
  return MV4(0x4633c3, out, in, count, bits);
}
void bb_movie_iqt_blocks_entry(const void *indices, const void *previous,
                               void *out, uint32_t reuse, const void *offsets,
                               const void *literals, uint32_t stride,
                               uint32_t cols, uint32_t rows) {
  BB_GAME_CALL(0x463464, 0, MV_WORD(indices), MV_WORD(previous), MV_WORD(out),
               reuse, MV_WORD(offsets), MV_WORD(literals), stride, cols, rows);
}
#endif
