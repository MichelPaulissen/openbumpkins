/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#include <string.h>

#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif

/* Extended-to-IEEE conversion uses integer C arithmetic. The CRT parser's
 * twelve-byte input has an extra low word beyond an x87 ten-byte number.
 * Text parsing and decimal generation both use the shared C dispatcher. */
static uint32_t stack(const uint32_t *input) { return input[3] + 2052u; }
static uint32_t arg(const uint32_t *input, unsigned n) {
  return BB_U32(stack(input) + 4u * (n + 1u));
}
static void call(uint32_t *input, uintptr_t target, uint32_t args[16]) {
  struct bb_call_state state = {input[7], input[6], input[5], input[8]};
  bb_qmix_call(&state, target, args, stack(input) - 128u, input[0], input[1],
               input[2], input[4], 0);
  input[7] = state.eax;
  input[6] = state.ecx;
  input[5] = state.edx;
  input[8] = state.flags;
}
static void load_words(uint32_t p, uint32_t words[3]) {
  for (unsigned i = 0; i < 3; ++i) words[i] = BB_U32(p + i * 4u);
}
static void store_words(uint32_t p, const uint32_t words[3]) {
  for (unsigned i = 0; i < 3; ++i) BB_U32(p + i * 4u) = words[i];
}
/* These triples run from the most significant word to the least significant
 * word, unlike the packed extended inputs and the separate add primitive. */
static unsigned tail_zero(const uint32_t words[3], unsigned bit) {
  unsigned index = bit / 32u, shift = 31u - bit % 32u;
  if (words[index] & ~(UINT32_MAX << shift)) return 0;
  for (++index; index < 3; ++index)
    if (words[index]) return 0;
  return 1;
}
static unsigned increment(uint32_t words[3], unsigned bit) {
  int index = (int)(bit / 32u);
  uint64_t sum = (uint64_t)words[index] + (UINT32_C(1) << (31u - bit % 32u));
  words[index] = (uint32_t)sum;
  unsigned carry = (unsigned)(sum >> 32);
  while (--index >= 0 && carry) {
    sum = (uint64_t)words[index] + carry;
    words[index] = (uint32_t)sum;
    carry = (unsigned)(sum >> 32);
  }
  return carry;
}
static unsigned round_words(uint32_t words[3], unsigned precision) {
  unsigned index = precision / 32u, shift = 31u - precision % 32u;
  unsigned carry = 0;
  /* Keep the original guard/tail convention, including its exclusion of the
   * next indexed bit. Exact x87 rounding is not needed for playback. */
  if ((words[index] & (UINT32_C(1) << shift)) &&
      !tail_zero(words, precision + 1u))
    carry = increment(words, precision - 1u);
  words[index] &= UINT32_MAX << shift;
  for (++index; index < 3; ++index) words[index] = 0;
  return carry;
}
static void shift_words(uint32_t words[3], unsigned count) {
  unsigned whole = count / 32u, partial = count % 32u;
  uint32_t result[3] = {0};
  for (unsigned i = whole; i < 3; ++i) {
    result[i] = words[i - whole] >> partial;
    if (partial && i > whole)
      result[i] |= words[i - whole - 1u] << (32u - partial);
  }
  memcpy(words, result, sizeof(result));
}
/* 0x10016120..0x10016184: all less significant bits below an index are zero. */
void bb_qmix_convert_tail_zero_c(uint32_t *input) {
  uint32_t words[3]; load_words(arg(input, 0), words);
  input[7] = tail_zero(words, arg(input, 1));
}
/* 0x10016190..0x100161f3: increment a selected bit with upward carry. */
void bb_qmix_convert_increment_c(uint32_t *input) {
  uint32_t p = arg(input, 0), words[3]; load_words(p, words);
  input[7] = increment(words, arg(input, 1)); store_words(p, words);
}
/* 0x10016200..0x100162ac: round/truncate a high-word-first mantissa. */
void bb_qmix_convert_round_c(uint32_t *input) {
  uint32_t p = arg(input, 0), words[3]; load_words(p, words);
  input[7] = round_words(words, arg(input, 1)); store_words(p, words);
}
/* 0x100162b0..0x100162cd: forward word copy, including its overlap ordering. */
void bb_qmix_convert_copy_c(uint32_t *input) {
  uint32_t out = arg(input, 0), source = arg(input, 1);
  for (unsigned i = 0; i < 3; ++i)
    BB_U32(out + 4u * i) = BB_U32(source + 4u * i);
}
/* 0x100162d0..0x100162df and 0x100162e0..0x100162fd: clear/zero query. */
void bb_qmix_convert_clear_c(uint32_t *input) {
  uint32_t p = arg(input, 0);
  for (unsigned i = 0; i < 3; ++i) BB_U32(p + 4u * i) = 0;
  input[7] = 0;
}
void bb_qmix_convert_zero_c(uint32_t *input) {
  uint32_t p = arg(input, 0);
  input[7] = !(BB_U32(p) | BB_U32(p + 4) | BB_U32(p + 8));
}
/* 0x10016300..0x100163ad: arbitrary right shift of a 96-bit mantissa. */
void bb_qmix_convert_shift_c(uint32_t *input) {
  uint32_t p = arg(input, 0), words[3]; load_words(p, words);
  shift_words(words, arg(input, 1)); store_words(p, words);
}
/* 0x100163b0..0x1001657b: parameterized extended-to-binary conversion.
 * Status 1 is overflow, 2 is underflow, and 0 is ordinary conversion. */
void bb_qmix_convert_binary_c(uint32_t *input) {
  uint32_t source = arg(input, 0), out = arg(input, 1), format = arg(input, 2);
  uint16_t encoded = BB_U16(source + 10);
  int32_t exponent = (int32_t)(encoded & 0x7fffu) - 0x3fff;
  uint32_t words[3] = {BB_U32(source + 6), BB_U32(source + 2),
                       (uint32_t)BB_U16(source) << 16};
  uint32_t status = 0;
  unsigned precision = BB_U32(format + 8), fraction = BB_U32(format + 12);
  int32_t minimum = (int32_t)BB_U32(format + 4);
  int32_t maximum = (int32_t)BB_U32(format);
  if (exponent == -0x3fff) {
    exponent = 0;
    if (words[0] | words[1] | words[2]) {
      memset(words, 0, sizeof(words)); status = 2;
    }
  } else {
    uint32_t unrounded[3]; memcpy(unrounded, words, sizeof(words));
    if (round_words(words, precision)) ++exponent;
    if (exponent < minimum - (int32_t)precision) {
      memset(words, 0, sizeof(words)); exponent = 0; status = 2;
    } else if (exponent <= minimum) {
      memcpy(words, unrounded, sizeof(words));
      shift_words(words, (unsigned)(minimum - exponent));
      round_words(words, precision);
      shift_words(words, fraction + 1u);
      exponent = 0; status = 2;
    } else if (exponent >= maximum) {
      words[0] = 0x80000000u; words[1] = words[2] = 0;
      shift_words(words, fraction);
      exponent = maximum + (int32_t)BB_U32(format + 20); status = 1;
    } else {
      exponent += (int32_t)BB_U32(format + 20);
      words[0] &= 0x7fffffffu;
      shift_words(words, fraction);
    }
  }
  uint32_t high = ((uint32_t)exponent << ((31u - fraction) & 31u)) |
                  ((encoded & 0x8000u) ? 0x80000000u : 0u) | words[0];
  unsigned width = BB_U32(format + 16);
  if (width == 64) { BB_U32(out) = words[1]; BB_U32(out + 4) = high; }
  else if (width == 32) BB_U32(out) = high;
  input[7] = status;
}
/* 0x10016580..0x10016598 and 0x100165a0..0x100165b8: format adapters. */
static void binary_adapter(uint32_t *input, uint32_t format) {
  uint32_t args[16] = {arg(input, 0), arg(input, 1), format};
  call(input, 0x100163b0u, args);
}
void bb_qmix_convert_double_c(uint32_t *input) {
  binary_adapter(input, 0x10020470u);
}
void bb_qmix_convert_float_c(uint32_t *input) {
  binary_adapter(input, 0x10020488u);
}
/* 0x100165c0..0x100165f5 and 0x10016600..0x10016635: run the existing
 * text parser, then store its extended result through the C converter. */
static void parse(uint32_t *input, unsigned wide) {
  uint32_t s = stack(input), out = arg(input, 0);
  uint32_t args[16] = {s - 12u, s - 16u, arg(input, 1), 0, 0, 0, 0};
  call(input, 0x10018660u, args);
  args[0] = s - 12u; args[1] = out;
  call(input, wide ? 0x10016580u : 0x100165a0u, args);
}
void bb_qmix_convert_parse_double_c(uint32_t *input) { parse(input, 1); }
void bb_qmix_convert_parse_float_c(uint32_t *input) { parse(input, 0); }
/* 0x100166d0..0x10016737: pack and generate digits through the C dispatcher,
 * then populate the CRT's shared mutable decimal record. */
void bb_qmix_convert_decimal_record_c(uint32_t *input) {
  uint32_t s = stack(input), temporary = s - 12u;
  uint32_t args[16] = {temporary, s + 4u};
  call(input, 0x10016740u, args);
  args[0] = BB_U32(temporary); args[1] = BB_U32(temporary + 4);
  args[2] = BB_U32(temporary + 8); args[3] = 17;
  args[4] = 0; args[5] = 0x10020d30u;
  call(input, 0x10018db0u, args);
  BB_U32(0x10020d58u) = input[7];
  BB_U32(0x10020d5cu) = 0x10020d34u;
  BB_U32(0x10020d50u) = (uint32_t)(int32_t)(int8_t)BB_U8(0x10020d32u);
  BB_U32(0x10020d54u) = (uint32_t)(int32_t)(int16_t)BB_U16(0x10020d30u);
  input[7] = 0x10020d50u;
}
/* 0x10018450..0x10018473: store a 32-bit sum and return its carry. */
void bb_qmix_convert_add_c(uint32_t *input) {
  uint64_t sum = (uint64_t)arg(input, 0) + arg(input, 1);
  BB_U32(arg(input, 2)) = (uint32_t)sum;
  input[7] = (uint32_t)(sum >> 32);
}

#define ARG(n) uint32_t a##n __attribute__((unused))
#define ENTRY(name, signature)                                                 \
  uint32_t __attribute__((naked)) name##_entry signature {                     \
    __asm__ volatile(                                                          \
        "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(         \
            name##_c) ";leal 4(%esp),%esp;popal;popfl;leal 2048(%esp),%esp;ret"); \
  }
ENTRY(bb_qmix_convert_tail_zero, (ARG(0), ARG(1)))
ENTRY(bb_qmix_convert_increment, (ARG(0), ARG(1)))
ENTRY(bb_qmix_convert_round, (ARG(0), ARG(1)))
ENTRY(bb_qmix_convert_copy, (ARG(0), ARG(1)))
ENTRY(bb_qmix_convert_clear, (ARG(0)))
ENTRY(bb_qmix_convert_zero, (ARG(0)))
ENTRY(bb_qmix_convert_shift, (ARG(0), ARG(1)))
ENTRY(bb_qmix_convert_binary, (ARG(0), ARG(1), ARG(2)))
ENTRY(bb_qmix_convert_double, (ARG(0), ARG(1)))
ENTRY(bb_qmix_convert_float, (ARG(0), ARG(1)))
ENTRY(bb_qmix_convert_parse_double, (ARG(0), ARG(1)))
ENTRY(bb_qmix_convert_parse_float, (ARG(0), ARG(1)))
ENTRY(bb_qmix_convert_decimal_record, (ARG(0), ARG(1)))
ENTRY(bb_qmix_convert_add, (ARG(0), ARG(1), ARG(2)))
#undef ENTRY
#undef ARG
