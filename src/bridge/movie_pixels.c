/* SPDX-License-Identifier: GPL-3.0-only */
#include "movie_pixels.h"
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include "movie_calls.h"
#endif
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif
#define FLOAT(a) BB_FLOAT(a)
static uint32_t high_product(uint32_t a, uint32_t b) {
  return (uint64_t)((int64_t)(int32_t)a * (int32_t)b) >> 32;
}
static unsigned parity(uint32_t value) {
  value ^= value >> 4;
  return (0x9669 >> (value & 15)) & 1;
}
static uint32_t arithmetic_flags(uint32_t flags, uint32_t a, uint32_t b,
                                 unsigned subtract) {
  uint32_t result = subtract ? a - b : a + b;
  uint32_t carry = subtract ? a < b : result < a;
  uint32_t overflow =
      subtract ? (a ^ b) & (a ^ result) : ~(a ^ b) & (a ^ result);
  return (flags & ~0x8d5u) | carry | (parity(result) << 2) |
         ((a ^ b ^ result) & 16) | ((result == 0) << 6) |
         ((result >> 31) << 7) | ((overflow >> 31) << 11);
}
/* 0x45d460: each chroma value colors a 2x2 group, in upper/upper/lower/lower
 * store order. Source planes and channel tables stay live across writes. */
void BB_FASTCALL __attribute__((noinline))
bb_movie_chroma_convert_entry(void *output, uint32_t width) {
  uint32_t top = bb_address_encode(output);
  for (uint32_t row = 0; row < 8; ++row, top += width * 8)
    for (uint32_t column = 0; column < 8; ++column) {
      uint32_t chroma = (row * 8 + column) * 4;
      int32_t v = (int32_t)BB_U32(0x6be8d0 + chroma) >> 16;
      int32_t u = (int32_t)BB_U32(0x6be7d0 + chroma) >> 16;
      uint32_t red = 0x6bd458 + (uint32_t)((v * 0x662) >> 10) * 4;
      uint32_t blue = 0x6bb380 + (uint32_t)((u * 0x811) >> 10) * 4;
      uint32_t green = 0x6bc9f0 - (uint32_t)((u * 0x191 + v * 0x340) >> 10) * 4;
      uint32_t luma = 0x6be3d0 + row * 128 + column * 8;
      uint32_t destination = top + column * 8;
      for (unsigned pixel = 0; pixel < 4; ++pixel) {
        uint32_t offset = (uint32_t)((int32_t)BB_U32(luma + (pixel & 1) * 4 +
                                                     (pixel & 2 ? 64 : 0)) >>
                                     16) *
                          4;
        uint32_t color = BB_U32(red + offset);
        color |= BB_U32(blue + offset);
        color |= BB_U32(green + offset);
        BB_U32(destination + (pixel & 1) * 4 + (pixel & 2 ? width * 4 : 0)) =
            color;
      }
    }
}
/* Saved PUSHAL registers followed by flags: EDI, ESI, EBP, ESP, EBX, EDX,
 * ECX, EAX, EFLAGS. These leaf boundaries carry no game behavior in assembly.
 */
#ifndef BB_MODEL_RUNTIME
#define PIXEL_ENTRY(name, body)                                                \
  void __attribute__((naked, noinline)) name(void) {                           \
    __asm__ volatile("pushfl;pushal;cld;pushl %esp;call " BB_ASM(              \
        body) ";addl $4,%esp;popal;popfl;ret");                                \
  }
#else
#define PIXEL_ENTRY(...)
#endif
/* 0x45e4c3: two luma words share one chroma pair; refresh live inputs/tables
 * after each pair, including when output aliases either source or the tables.
 */
void __attribute__((used, noinline)) bb_movie_color_row_c(uint32_t *r) {
  uint32_t output = r[0], luma = r[1], chroma = r[4];
  for (uint32_t pair = 0; pair < 8; ++pair) {
    if (pair == 0 || pair == 4)
      (void)BB_U32(output + pair * 8);
    uint32_t a = (BB_U32(luma + pair * 8) >> 17) & 127;
    uint32_t b = (BB_U32(luma + pair * 8 + 4) >> 17) & 127;
    uint32_t u = (BB_U32(chroma + pair * 4) >> 17) & 127;
    uint32_t v = (BB_U32(chroma + pair * 4 + 256) >> 17) & 127;
    a = BB_U32(0x6cc25c + a * 4);
    b = BB_U32(0x6cc25c + b * 4);
    u = BB_U32(0x6cc45c + u * 4);
    v = BB_U32(0x6cc65c + v * 4);
    uint32_t first = a + u + v, second = b + u + v;
    BB_U32(output + pair * 8) = first;
    BB_U32(output + pair * 8 + 4) = second;
    r[7] = first;
    r[5] = second;
    r[6] = u;
    r[2] = v;
    r[8] = arithmetic_flags(r[8], b + u, v, 0);
  }
}
PIXEL_ENTRY(bb_movie_color_row_entry, bb_movie_color_row_c)

/* Complete odd/even inverse transform, with the original scratch stores and
 * later live reads. The large floating bias converts through double bits. */
static void idct(uint32_t *r, unsigned column) {
  uint32_t input = r[1];
  if (!column)
    (void)BB_U32(BB_U32(0x6cc014));
  uint32_t b = BB_U32(input + 20), a = BB_U32(input + 12);
  uint32_t c = BB_U32(input + 4), d = BB_U32(input + 28);
  if (column) {
    uint32_t test = b | a | c | d;
    uint32_t even2 = BB_U32(input + 8), even6 = BB_U32(input + 24);
    test |= even2 | even6;
    uint32_t even4 = BB_U32(input + 16);
    test |= even4;
    if (!test) {
      uint32_t output = BB_U32(0x6cc014);
      uint32_t dc = BB_U32(input);
      (void)BB_U32(output);
      for (unsigned i = 0; i < 8; ++i)
        BB_U32(output + i * 36) = dc;
      r[0] = output;
      r[2] = even6;
      r[4] = b;
      r[5] = test;
      r[6] = even4;
      r[7] = dc;
      r[8] = (r[8] & ~0x8d5u) | 0x44;
      return;
    }
  }
  uint32_t odd0 = a + b, odd1 = d + c, total = odd0 + odd1;
  b -= a;
  c -= d;
  BB_U32(0x6cc018) = b;
  BB_U32(0x6cc01c) = c;
  /* Keep intermediates in exact extended storage so the compiler cannot use
   * the caller's live x87 stack slots as additional temporaries. */
  volatile long double left = (int32_t)BB_U32(0x6cc018);
  volatile long double right = (int32_t)BB_U32(0x6cc01c);
  volatile long double term0 = right * (long double)FLOAT(0x6cc030);
  volatile long double term1 = left * (long double)FLOAT(0x6cc02c);
  volatile long double common = (left + right) * (long double)FLOAT(0x6cc034);
  uint32_t high = high_product(odd1 - odd0, BB_U32(0x6cc028)) << 1;
  volatile long double positive = term1 + common + (long double)FLOAT(0x6cc038);
  volatile long double negative = term0 - common + (long double)FLOAT(0x6cc038);
  a = BB_U32(input + 8);
  b = BB_U32(input + 24);
  BB_DOUBLE(0x6cc020) = (double)negative;
  BB_DOUBLE(0x6cc018) = (double)positive;
  c = BB_U32(0x6cc018) + high;
  uint32_t extra = BB_U32(0x6cc020);
  d = high + extra;
  extra += total;
  BB_U32(0x6cc01c) = c;
  BB_U32(0x6cc020) = d;
  BB_U32(0x6cc024) = extra;
  c = BB_U32(input);
  d = BB_U32(input + 16);
  uint32_t even = b + a;
  a -= b;
  b = d + c;
  c -= d;
  high = high_product(a, BB_U32(0x6cc028)) << 1;
  uint32_t output = BB_U32(0x6cc014);
  even += high;
  uint32_t saved = BB_U32(0x6cc01c);
  a = high + c;
  c -= high;
  d = even + b;
  b -= even;
  even = c + saved;
  c -= saved;
  saved = BB_U32(0x6cc018);
  uint32_t stride = column ? 36 : 4;
  BB_U32(output + stride * 2) = even;
  even = BB_U32(0x6cc020);
  BB_U32(output + stride * 5) = c;
  c = b + saved;
  b -= saved;
  saved = BB_U32(0x6cc024);
  BB_U32(output + stride * 3) = c;
  BB_U32(output + stride * 4) = b;
  b = even + a;
  a -= even;
  c = d + saved;
  uint32_t flags = arithmetic_flags(r[8], d, saved, 1);
  d -= saved;
  BB_U32(output) = c;
  BB_U32(output + stride) = b;
  BB_U32(output + stride * 6) = a;
  BB_U32(output + stride * 7) = d;
  r[0] = output;
  r[1] = even;
  r[2] = saved;
  r[4] = b;
  r[5] = d;
  r[6] = c;
  r[7] = a;
  r[8] = flags;
}
void __attribute__((used, noinline)) bb_movie_idct_column_c(uint32_t *r) {
  idct(r, 1);
}
void __attribute__((used, noinline)) bb_movie_idct_row_c(uint32_t *r) {
  idct(r, 0);
}
PIXEL_ENTRY(bb_movie_idct_column_entry, bb_movie_idct_column_c)
PIXEL_ENTRY(bb_movie_idct_row_entry, bb_movie_idct_row_c)
