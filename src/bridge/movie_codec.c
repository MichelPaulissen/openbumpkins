/* SPDX-License-Identifier: GPL-3.0-only */
#include "movie_codec.h"
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include "movie_calls.h"
#endif
#include "movie_pixels.h"
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif
#define BYTE(a) BB_U8(a)
static uint32_t shl(uint32_t value, uint32_t count) {
  return value << (count & 31);
}
static uint32_t sar(uint32_t value, uint32_t count) {
  return (uint32_t)((int32_t)value >> (count & 31));
}
static unsigned parity(uint32_t value) {
  value ^= value >> 4;
  return (0x9669 >> (value & 15)) & 1;
}
static uint32_t subtract_flags(uint32_t flags, uint32_t a, uint32_t b) {
  uint32_t result = a - b;
  return (flags & ~0x8d5u) | (a < b) | (parity(result) << 2) |
         ((a ^ b ^ result) & 16) | ((result == 0) << 6) |
         ((result >> 31) << 7) | ((((a ^ b) & (a ^ result)) >> 31) << 11);
}
static uint32_t fixed_product(uint32_t value, uint32_t scale, uint32_t *high) {
  int64_t product = (int64_t)(int32_t)value * (int32_t)scale;
  *high = (uint32_t)((uint64_t)product >> 32) << 16;
  return (uint32_t)((uint64_t)product >> 16) + ((uint64_t)product >> 15 & 1);
}
/* 0x45dd95: little-endian buffered three/eight-bit coefficient commands.
 * The zigzag position and byte count are independent wrapped counters. */
void __attribute__((used, noinline)) bb_movie_rle_coefficients_c(uint32_t *r) {
  uint32_t buffer = BB_U32(0x6cc004), bits = BB_U32(0x6cc008);
  uint32_t cursor = BB_U32(0x6cc000);
  uint32_t a, index = 0, high, position = 63;
  a = fixed_product(sar(buffer << 24, 8), BB_U32(0x6cc15c), &high);
  BB_U32(0x6be1b0) = a;
  buffer >>= 8;
  a = shl(BYTE(cursor++), bits - 8);
  buffer |= a;
  do {
    index = BB_U32(0x6cc05c + position * 4);
    a = buffer & 7;
    unsigned wide = 0;
    if (a == 0)
      BB_U32(0x6be1b0 + index * 4) = 0;
    else if (a == 2 || a == 6) {
      a = BB_U32(0x6cc15c + index * 4);
      if ((buffer & 7) == 6)
        a = 0 - a;
      BB_U32(0x6be1b0 + index * 4) = a;
    } else if (a == 4) {
      BB_U32(0x6be1b0 + index * 4) = 0;
      index = BB_U32(0x6cc058 + position * 4);
      --position;
      BB_U32(0x6be1b0 + index * 4) = 0;
    } else if (a == 1 || a == 5) {
      if (a == 5) {
        BB_U32(0x6be1b0 + index * 4) = 0;
        index = BB_U32(0x6cc058 + position * 4);
        --position;
      }
      a = (buffer & 255) >> 3;
      high = 0;
      do {
        BB_U32(0x6be1b0 + index * 4) = 0;
        index = BB_U32(0x6cc058 + position * 4);
        BB_U32(0x6be1b0 + index * 4) = 0;
        index = BB_U32(0x6cc054 + position * 4);
        position -= 2;
      } while (--a);
      ++position;
      wide = 1;
    } else {
      a = buffer & 252;
      if (a == 252) {
        buffer >>= 8;
        a = shl(BYTE(cursor++), bits - 8);
        buffer |= a;
        a = sar(buffer << 24, 8);
      } else
        a = sar(a << 24, 10);
      a = fixed_product(a, BB_U32(0x6cc15c + index * 4), &high);
      BB_U32(0x6be1b0 + index * 4) = a;
      wide = 1;
    }
    if (wide) {
      buffer >>= 8;
      a = shl(BYTE(cursor++), bits - 8);
      buffer |= a;
    } else {
      buffer >>= 3;
      bits -= 3;
      if ((int32_t)bits < 8) {
        a = shl(BYTE(cursor++), bits);
        bits += 8;
        buffer |= a;
      }
    }
    uint32_t before = position;
    --position;
    r[8] = subtract_flags(r[8], before, 1) & ~1u;
  } while ((int32_t)position > 0);
  BB_U32(0x6cc000) = cursor;
  BB_U32(0x6cc004) = buffer;
  BB_U32(0x6cc008) = bits;
  r[0] = position;
  r[1] = cursor;
  r[2] = buffer;
  r[4] = index;
  r[5] = high;
  r[6] = bits;
  r[7] = a;
}
/* Both DWORDs are fetched even for a zero SHLD count. */
static uint32_t window(uint32_t base, uint32_t bits, uint32_t *next) {
  uint32_t address = base + (bits >> 5) * 4;
  uint32_t first = BB_U32(address);
  *next = BB_U32(address + 4);
  unsigned shift = bits & 31;
  return shift ? (first << shift) | (*next >> (32 - shift)) : first;
}
/* 0x45df35: MSB-first VLC coefficients, run/level escape extensions and EOB.
 * EAX carries DC, EBP stream base and EDI the incoming bit position. */
void __attribute__((used, noinline)) bb_movie_vlc_coefficients_c(uint32_t *r) {
  uint32_t base = r[2], bits = r[0], position = 63;
  BB_U32(0x6be1b0) = r[7] * BB_U32(0x6cc15c);
  for (unsigned i = 1; i < 64; ++i)
    BB_U32(0x6be1b0 + i * 4) = 0;
  for (;;) {
    uint32_t next, code = window(base, bits, &next) >> 15;
    uint32_t descriptor;
    if (code >= 0x8000)
      descriptor = BB_U32(0x6cc858 + (code >> 13) * 4);
    else {
      uint32_t prefix = code >> 1;
      if (prefix >= 0x400)
        descriptor = BB_U32(0x6cc888 + (prefix >> 8) * 4);
      else if (prefix >= 0x200)
        descriptor = BB_U32(0x6cc968 + (prefix >> 6) * 4);
      else if (prefix >= 0x100)
        descriptor = BB_U32(0x6cc968 + (prefix >> 4) * 4);
      else if (prefix >= 0x80)
        descriptor = BB_U32(0x6cc9a8 + (prefix >> 3) * 4);
      else if (prefix >= 0x40)
        descriptor = BB_U32(0x6cc9e8 + (prefix >> 2) * 4);
      else if (prefix >= 0x20)
        descriptor = BB_U32(0x6cca28 + (prefix >> 1) * 4);
      else if (prefix >= 0x10)
        descriptor = BB_U32(0x6cca68 + prefix * 4);
      else {
        BB_U32(0x6cc008) = bits;
        r[0] = bits;
        r[4] = position;
        r[5] = next;
        r[6] = code;
        r[7] = prefix;
        r[8] = subtract_flags(r[8], prefix, 16);
        return;
      }
    }
    uint32_t length = sar(descriptor, 16);
    bits += length;
    uint32_t run = (descriptor >> 8) & 255;
    if (run == 65) {
      BB_U32(0x6cc008) = bits;
      r[0] = bits;
      r[4] = position;
      r[5] = descriptor;
      r[6] = code;
      r[7] = length;
      r[8] = (r[8] & ~0x8d5u) | 0x44;
      return;
    }
    uint32_t value, index;
    if (run < 65) {
      position = (position & ~255u) | ((position - run) & 255);
      value = descriptor & 255;
      if ((code >> ((16 - length) & 31)) & 1)
        value = 0 - value;
      ++bits;
      index = BB_U32(0x6cc060 + position * 4);
    } else {
      value = window(base, bits, &next) >> 18;
      bits += 14;
      position -= value >> 8;
      value &= 255;
      if (value == 0 || value == 128) {
        unsigned negative = value == 128;
        value = window(base, bits, &next) >> 24;
        bits += 8;
        if (negative)
          value -= 256;
      } else if (value > 128)
        value -= 256;
      index = BB_U32(0x6cc05c + position * 4);
      --position;
    }
    BB_U32(0x6be1b0 + index * 4) = value * BB_U32(0x6cc15c + index * 4);
  }
}
#ifndef BB_MODEL_RUNTIME
#define CODEC_ENTRY(name, body, parameters)                                    \
  void __attribute__((naked, noinline)) name parameters {                      \
    __asm__ volatile("pushfl;pushal;cld;pushl %esp;call " BB_ASM(              \
        body) ";addl $4,%esp;popal;popfl;ret");                                \
  }
#else
#define CODEC_ENTRY(...)
#endif
CODEC_ENTRY(bb_movie_rle_coefficients_entry, bb_movie_rle_coefficients_c,
            (void))
CODEC_ENTRY(bb_movie_vlc_coefficients_entry, bb_movie_vlc_coefficients_c,
            (void))

static void transform(uint32_t *r, unsigned block) {
  for (unsigned column = 0; column < 8; ++column) {
    r[1] = 0x6be1b0 + column * 32;
    BB_U32(0x6cc014) = 0x6be2b0 + column * 4;
    bb_movie_idct_column_c(r);
  }
  uint32_t output = block < 4 ? 0x6be3d0 + (block / 2) * 512 + (block % 2) * 32
                              : 0x6be7d0 + (block - 4) * 256;
  uint32_t stride = block < 4 ? 64 : 32;
  for (unsigned row = 0; row < 8; ++row) {
    r[1] = 0x6be2b0 + row * 36;
    BB_U32(0x6cc014) = output + row * stride;
    bb_movie_idct_row_c(r);
  }
}
static void color_rows(uint32_t *r, uint32_t output, uint32_t pitch) {
  for (unsigned row = 0; row < 16; ++row) {
    r[0] = output;
    r[1] = 0x6be3d0 + row * 64;
    r[4] = 0x6be7d0 + (row / 2) * 32;
    bb_movie_color_row_c(r);
    output += pitch;
  }
}
void __attribute__((used, noinline)) bb_movie_packet_block_c(uint32_t *r) {
  uint32_t original[9];
  for (unsigned i = 0; i < 9; ++i)
    original[i] = r[i];
  uint32_t input = BB_U32(original[3] + 8);
  uint32_t dc = BYTE(input++);
  BB_U32(0x6cc000) = input;
  BB_U32(0x6cc004) = dc;
  BB_U32(0x6cc008) = 8;
  for (unsigned block = 0; block < 6; ++block) {
    bb_movie_rle_coefficients_c(r);
    transform(r, block);
  }
  color_rows(r, BB_U32(original[3] + 12), BB_U32(original[3] + 16));
  for (unsigned i = 0; i < 7; ++i)
    r[i] = original[i];
}
CODEC_ENTRY(bb_movie_packet_block_entry, bb_movie_packet_block_c,
            (const void *input __attribute__((unused)),
             void *output __attribute__((unused)),
             uint32_t pitch __attribute__((unused))))

static void coded_block(uint32_t *r, unsigned block) {
  uint32_t base = BB_U32(0x6cc000), bits = BB_U32(0x6cc008), next;
  uint32_t prefix = window(base, bits, &next) >> (block < 4 ? 25 : 24);
  uint32_t code = BYTE((block < 4 ? 0x6ccae8 : 0x6ccb68) + prefix);
  uint32_t size = code >> 4;
  bits += code & 15;
  uint32_t dc = 0;
  if (size) {
    dc = window(base, bits, &next) >> ((32 - size) & 31);
    bits += size;
    if (!((dc >> ((size - 1) & 31)) & 1))
      dc -= shl(1, size) - 1;
  }
  uint32_t predictor = block < 4 ? 0x6cc85c : 0x6cc860 + (block - 4) * 4;
  r[2] = base;
  r[0] = bits;
  r[7] = dc + BB_U32(predictor);
  BB_U32(predictor) = r[7];
  bb_movie_vlc_coefficients_c(r);
  transform(r, block);
}
static void vlc_block(uint32_t *r, unsigned colors) {
  uint32_t original[9];
  for (unsigned i = 0; i < 9; ++i)
    original[i] = r[i];
  for (unsigned block = 0; block < 6; ++block)
    coded_block(r, block);
  if (colors)
    color_rows(r, BB_U32(original[3] + 8), BB_U32(original[3] + 12));
  for (unsigned i = 0; i < 7; ++i)
    r[i] = original[i];
}
void __attribute__((used, noinline)) bb_movie_block_32_c(uint32_t *r) {
  vlc_block(r, 1);
}
void __attribute__((used, noinline)) bb_movie_block_16_c(uint32_t *r) {
  vlc_block(r, 0);
}
CODEC_ENTRY(bb_movie_block_32_entry, bb_movie_block_32_c,
            (void *output __attribute__((unused)),
             uint32_t pitch __attribute__((unused))))
CODEC_ENTRY(bb_movie_block_16_entry, bb_movie_block_16_c,
            (void *output __attribute__((unused)),
             uint32_t pitch __attribute__((unused))))

#ifdef BB_MODEL_RUNTIME
void bb_movie_packet_block_entry(const void *in, void *out, uint32_t pitch) {
  MV3(0x45e9e9, in, out, pitch);
}
void bb_movie_block_32_entry(void *out, uint32_t pitch) {
  MV2(0x45fbdb, out, pitch);
}
void bb_movie_block_16_entry(void *out, uint32_t pitch) {
  MV2(0x460774, out, pitch);
}
#endif
