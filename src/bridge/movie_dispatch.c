/* SPDX-License-Identifier: GPL-3.0-only */
#include "movie_dispatch.h"
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include "movie_calls.h"
#endif
#include "movie_codec.h"
#include "movie_color.h"
#include "movie_iqt.h"
#include "movie_pixels.h"
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
static uint32_t fixed_round(uint32_t value, uint32_t coefficient) {
  int64_t product = (int64_t)(int32_t)value * (int32_t)coefficient;
  return (uint32_t)((uint64_t)product >> 16) + ((uint64_t)product >> 15 & 1);
}
/* 0x45c170: packet length is independent of command kind. Motion copies
 * traverse forwards and re-read the object's stride after every row. */
void BB_FASTCALL __attribute__((noinline))
bb_movie_packets_32_entry(void *object, const unsigned char *input,
                          void *output, const void *previous) {
  uint32_t p = bb_address_encode(object), stream = bb_address_encode(input);
  if (!BB_U32(p + 4))
    return;
  uint32_t y = 0;
  do {
    uint32_t width = BB_U32(p + 8), x = 0;
    if (width)
      do {
        uint32_t length = BYTE(stream++);
        uint32_t dst = bb_address_encode(output) + (y * width + x) * 4;
        if (length > 12)
          bb_movie_packet_block_entry((const void *)(uintptr_t)stream,
                                      (void *)(uintptr_t)dst, width * 4);
        else if (length == 1) {
          uint32_t packed = BYTE(stream);
          int32_t dx = (int32_t)(packed >> 4), dy = (int32_t)(packed & 15);
          if (dx > 7)
            dx -= 16;
          if (dy > 7)
            dy -= 16;
          uint32_t src = bb_address_encode(previous) +
                         (((y - (uint32_t)dy) * width + x - (uint32_t)dx) * 4);
          for (unsigned row = 0; row < 16; ++row) {
            for (unsigned column = 0; column < 16; ++column)
              BB_U32(dst + column * 4) = BB_U32(src + column * 4);
            uint32_t stride = BB_U32(p + 8) * 4;
            src += stride;
            dst += stride;
          }
        } else if (length == 3) {
          uint32_t luma = BYTE(stream);
          bb_movie_color_block_entry(luma, luma, luma, luma, BYTE(stream + 1),
                                     BYTE(stream + 2), (void *)(uintptr_t)dst,
                                     width * 4);
        } else if (length == 6) {
          bb_movie_color_block_entry(BYTE(stream), BYTE(stream + 1),
                                     BYTE(stream + 2), BYTE(stream + 3),
                                     BYTE(stream + 4), BYTE(stream + 5),
                                     (void *)(uintptr_t)dst, width * 4);
        } else if (length == 12) {
          bb_movie_color_block_entry(BYTE(stream), BYTE(stream + 2),
                                     BYTE(stream + 4), BYTE(stream + 6),
                                     BYTE(stream + 8), BYTE(stream + 10),
                                     (void *)(uintptr_t)dst, width * 4);
        }
        x += 16;
        stream += length;
        width = BB_U32(p + 8);
      } while (x < width);
    y += 16;
  } while (y < BB_U32(p + 4));
}
void __attribute__((noinline, used)) bb_movie_bit_reset_c(const void *input) {
  BB_U32(0x6cc000) = bb_address_encode(input);
  BB_U32(0x6cc008) = 0;
  BB_U32(0x6cc85c) = 0;
  BB_U32(0x6cc860) = 0;
  BB_U32(0x6cc864) = 0;
}
/* 0x45dd60 leaves incoming registers/flags and the buffered word untouched. */
#ifndef BB_MODEL_RUNTIME
void __attribute__((naked, noinline))
bb_movie_bit_reset_entry(const void *input __attribute__((unused))) {
#ifdef _WIN32
#define BB_RESET_SYMBOL "_bb_movie_bit_reset_c"
#else
#define BB_RESET_SYMBOL "bb_movie_bit_reset_c"
#endif
  __asm__ volatile("pushfl;pushal;pushl 40(%esp);call " BB_RESET_SYMBOL
                   ";addl $4,%esp;popal;popfl;ret");
#undef BB_RESET_SYMBOL
}
#else
void bb_movie_bit_reset_entry(const void *input) {
  bb_movie_bit_reset_c(input);
}
#endif
/* 0x45d150: IQT alternatives and packed/motion block codecs; quantization
 * tables are completed before any block callback runs. */
void BB_FASTCALL __attribute__((noinline))
bb_movie_frame_dispatch_entry(void *object, const uint32_t *slot) {
  uint32_t p = bb_address_encode(object),
           data = BB_U32(bb_address_encode(slot));
  uint32_t kind = BB_U32(bb_address_encode(slot) + 12);
  if (kind == 1) {
    bb_movie_iqt_init_entry(object, (const void *)(uintptr_t)data);
    bb_movie_iqt_palette_entry(object);
    bb_movie_iqt_expand_entry(object);
    return;
  }
  if (kind == 2) {
    bb_movie_iqt_delta_entry(object, (const void *)(uintptr_t)data);
    bb_movie_iqt_expand_entry(object);
    return;
  }
  if (kind != 3 && kind != 4)
    return;
  BB_U32(p + 8) = (int32_t)(int16_t)WORD(data);
  BB_U32(p + 4) = (int32_t)(int16_t)WORD(data + 2);
  uint32_t quality = BYTE(data + 4);
  BB_U32(p + 0x3a4) = quality;
  if (kind == 3) {
    int32_t remaining = 100 - (int32_t)quality;
    int32_t base = remaining * 22 / 100 + 8;
    int32_t delta = remaining * 50 / 100 + 10 - base;
    int32_t first = 0;
    for (uint32_t row = 0; row < 8; ++row, first += delta) {
      int32_t stair = first;
      for (uint32_t column = 0; column < 8; ++column, stair += delta) {
        uint32_t offset = (row * 8 + column) * 4;
        uint32_t coefficient = (uint32_t)(stair / 14 + base) << 16;
        BB_U32(0x6cc15c + offset) =
            fixed_round(BB_U32(0x48ba70 + offset), coefficient);
      }
    }
    void *output = BB_POINTER(p + 0x39c);
    const void *previous = BB_POINTER(p + 0x3a0);
    bb_movie_packets_32_entry(object, (const void *)(uintptr_t)(data + 8),
                              output, previous);
    return;
  }
  BB_U32(p + 0x3b0) = BYTE(data + 7) & 2;
  if (quality == 100) {
    for (uint32_t offset = 0; offset < 256; offset += 4)
      BB_U32(0x6cc15c + offset) =
          fixed_round(BB_U32(0x48ba70 + offset), 0x80000);
  } else {
    volatile float factor = ((long double)BB_DOUBLE(0x482368) - quality) *
                            (long double)BB_FLOAT(0x482370);
    for (uint32_t offset = 0; offset < 256; offset += 4) {
      uint32_t matrix = BB_U32(0x48ba70 + offset);
      long double value =
          (long double)(int32_t)BB_U32(0x48bb70 + offset) * factor;
      value *= (long double)BB_FLOAT(0x482374);
      BB_U32(0x6cc15c + offset) = fixed_round(matrix, (uint32_t)(int64_t)value);
    }
    BB_U32(0x6cc15c) = fixed_round(BB_U32(0x48ba70), 0x80000);
  }
  uint32_t output = BB_U32(p + 0x39c);
  bb_movie_bit_reset_entry((const void *)(uintptr_t)(data + 8));
  uint32_t packed = BB_U32(p + 0x3b0), y = 0;
  if (!BB_U32(p + 4))
    return;
  do {
    uint32_t width = BB_U32(p + 8), x = 0;
    if (width)
      do {
        if (packed) {
          bb_movie_block_16_entry(
              (void *)(uintptr_t)(output + (y * width + x) * 2), width * 2);
          width = BB_U32(p + 8);
          bb_movie_chroma_convert_entry(
              (void *)(uintptr_t)(output + (y * width + x) * 4), width);
        } else
          bb_movie_block_32_entry(
              (void *)(uintptr_t)(output + (y * width + x) * 4), width * 4);
        x += 16;
        width = BB_U32(p + 8);
      } while (x < width);
    y += 16;
  } while (y < BB_U32(p + 4));
}
