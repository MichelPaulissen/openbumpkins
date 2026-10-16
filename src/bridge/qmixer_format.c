/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#include <math.h>
#include <string.h>

#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif

/* Numeric formatting callbacks in C. Their decimal-record and binary parser
 * adapters, digit generation and text parsing use the shared C dispatcher.
 * Character locales use C NLS adapters and the platform API boundary. */
static uint32_t stack(const uint32_t *input) { return input[3] + 2052u; }
static uint32_t arg(const uint32_t *input, unsigned n) {
  return BB_U32(stack(input) + 4u * (n + 1u));
}
static char *text(uint32_t address) { return (char *)(uintptr_t)address; }
static void call(uint32_t *input, uintptr_t target, uint32_t args[16]) {
  struct bb_call_state state = {input[7], input[6], input[5], input[8]};
  bb_qmix_call(&state, target, args, stack(input) - 128u, input[0], input[1],
               input[2], input[4], 0);
  input[7] = state.eax;
  input[6] = state.ecx;
  input[5] = state.edx;
  input[8] = state.flags;
}
static uint32_t digits(uint32_t *input, uint32_t number) {
  uint32_t args[16] = {BB_U32(number), BB_U32(number + 4)};
  call(input, 0x100166d0u, args);
  return input[7];
}
static void round_digits(uint32_t *input, uint32_t out, int32_t count,
                         uint32_t record) {
  uint32_t args[16] = {out, (uint32_t)count, record};
  call(input, 0x10016640u, args);
}
static void shift(uint32_t *input, uint32_t address, uint32_t amount) {
  uint32_t args[16] = {address, amount};
  call(input, 0x10014840u, args);
}
static unsigned negative(uint32_t record) { return BB_U32(record) == '-'; }
static int32_t position(uint32_t record) { return (int32_t)BB_U32(record + 4); }
static unsigned cached(void) { return BB_U8(0x1001fe38u) != 0; }
static char decimal(void) { return (char)BB_U8(0x10020258u); }

/* 0x10014340..0x100143a5: insert the locale separator after the leading
 * digit run, or before an initial exponent marker. */
void bb_qmix_format_decimal_insert_c(uint32_t *input) {
  uint32_t p = arg(input, 0), args[16] = {(uint32_t)(int32_t)(int8_t)BB_U8(p)};
  call(input, 0x10016030u, args);
  if (input[7] != 'e') {
    do {
      ++p;
      int32_t ch = (int8_t)BB_U8(p);
      if ((int32_t)BB_U32(0x10020254u) > 1) {
        args[0] = (uint32_t)ch; args[1] = 4;
        call(input, 0x10015f90u, args);
      } else
        input[7] = BB_U16(BB_U32(0x10020260u) + (uint32_t)(ch * 2)) & 4u;
    } while (input[7]);
  }
  memmove(text(p + 1), text(p), strlen(text(p)) + 1);
  BB_U8(p) = (uint8_t)decimal();
}
/* 0x100143b0..0x10014409: trim fractional zeroes, preserving the exponent. */
void bb_qmix_format_trim_c(uint32_t *input) {
  char *p = text(arg(input, 0));
  while (*p && *p != decimal()) ++p;
  if (!*p) return;
  char *end = p + 1;
  while (*end && *end != 'e' && *end != 'E') ++end;
  char *last = end - 1;
  while (*last == '0') --last;
  if (*last == decimal()) --last;
  memmove(last + 1, end, strlen(end) + 1);
}
/* 0x10014410..0x10014427: zero and unordered values are nonnegative. */
void bb_qmix_format_nonnegative_c(uint32_t *input) {
  double value;
  memcpy(&value, text(arg(input, 0)), sizeof(value));
  input[7] = !(value < 0.0);
}
/* 0x10014430..0x1001447c: parser callback stores a double or float. */
void bb_qmix_format_parse_c(uint32_t *input) {
  uint32_t out = arg(input, 1), temporary = stack(input) - 16u;
  unsigned wide = arg(input, 0) != 0;
  uint32_t args[16] = {temporary, arg(input, 2)};
  call(input, wide ? 0x100165c0u : 0x10016600u, args);
  memcpy(text(out), text(temporary), wide ? 8 : 4);
}
/* 0x10016640..0x100166c1: round a digit string with a leading carry slot.
 * Negative requested counts produce an empty string, without rounding. */
void bb_qmix_format_digits_round_c(uint32_t *input) {
  char *out = text(arg(input, 0)), *p = out + 1;
  int32_t count = (int32_t)arg(input, 1);
  uint32_t record = arg(input, 2);
  const char *source = text(BB_U32(record + 12));
  *out = '0';
  for (int32_t n = 0; n < count; ++n)
    *p++ = *source ? *source++ : '0';
  *p = 0;
  if (count >= 0 && *source >= '5') {
    while (*--p == '9') *p = '0';
    ++*p;
  }
  if (*out == '1')
    ++BB_U32(record + 4);
  else
    memmove(out, out + 1, strlen(out + 1) + 1);
}
/* 0x10014840..0x1001486b: insert space in an overlapping string. */
void bb_qmix_format_shift_c(uint32_t *input) {
  uint32_t p = arg(input, 0), amount = arg(input, 1);
  if (amount) memmove(text(p + amount), text(p), strlen(text(p)) + 1);
}
/* 0x10014480..0x100145b6: scientific format with a three-digit exponent. */
void bb_qmix_format_scientific_c(uint32_t *input) {
  uint32_t out = arg(input, 1), p = out, record;
  int32_t precision = (int32_t)arg(input, 2);
  unsigned reused = cached();
  if (reused) {
    record = BB_U32(0x10020c08u);
    shift(input, out + negative(record), precision > 0);
  } else {
    record = digits(input, arg(input, 0));
    round_digits(input, out + negative(record) + (precision > 0),
                 precision + 1, record);
  }
  if (negative(record)) BB_U8(p++) = '-';
  if (precision > 0) {
    BB_U8(p) = BB_U8(p + 1);
    BB_U8(p + 1) = (uint8_t)decimal();
    ++p;
  }
  uint32_t exponent = p + (uint32_t)precision + !reused;
  memcpy(text(exponent), text(0x1001a900u), 6);
  if (arg(input, 3)) BB_U8(exponent) = 'E';
  if (BB_U8(BB_U32(record + 12)) != '0') {
    int32_t value = position(record) - 1;
    if (value < 0) {
      value = -value;
      BB_U8(exponent + 1) = '-';
    }
    if (value >= 100) {
      BB_U8(exponent + 2) += (uint8_t)(value / 100);
      value %= 100;
    }
    if (value >= 10) {
      BB_U8(exponent + 3) += (uint8_t)(value / 10);
      value %= 10;
    }
    BB_U8(exponent + 4) += (uint8_t)value;
  }
  input[7] = out;
}
/* 0x100145c0..0x100146bb: fixed format, leading zeroes and locale point. */
void bb_qmix_format_fixed_c(uint32_t *input) {
  uint32_t out = arg(input, 1), p = out, record;
  int32_t precision = (int32_t)arg(input, 2);
  unsigned reused = cached();
  if (reused) {
    record = BB_U32(0x10020c08u);
    if ((int32_t)BB_U32(0x1001fe3cu) == precision) {
      BB_U8(out + negative(record) + (uint32_t)precision) = '0';
      BB_U8(out + negative(record) + (uint32_t)precision + 1) = 0;
    }
  } else {
    record = digits(input, arg(input, 0));
    round_digits(input, out + negative(record), position(record) + precision,
                 record);
  }
  if (negative(record)) BB_U8(p++) = '-';
  int32_t point = position(record);
  if (point <= 0) {
    shift(input, p, 1);
    BB_U8(p++) = '0';
  } else
    p += (uint32_t)point;
  if (precision > 0) {
    shift(input, p, 1);
    BB_U8(p++) = (uint8_t)decimal();
    point = position(record);
    if (point < 0) {
      uint32_t zeroes = (uint32_t)-point;
      if (!reused && zeroes > (uint32_t)precision) zeroes = precision;
      shift(input, p, zeroes);
      memset(text(p), '0', zeroes);
    }
  }
  input[7] = out;
}
/* 0x10014770..0x1001479b and 0x100147a0..0x100147c6: reuse a decimal
 * record already rounded by the general formatter, then clear its mode. */
void bb_qmix_format_scientific_cached_c(uint32_t *input) {
  uint32_t args[16] = {arg(input, 0), arg(input, 1), arg(input, 2), arg(input, 3)};
  BB_U8(0x1001fe38u) = 1;
  call(input, 0x10014480u, args);
  BB_U8(0x1001fe38u) = 0;
}
void bb_qmix_format_fixed_cached_c(uint32_t *input) {
  uint32_t args[16] = {arg(input, 0), arg(input, 1), arg(input, 2)};
  BB_U8(0x1001fe38u) = 1;
  call(input, 0x100145c0u, args);
  BB_U8(0x1001fe38u) = 0;
}
/* 0x100146c0..0x10014770: select scientific/fixed form after rounding. */
void bb_qmix_format_general_c(uint32_t *input) {
  uint32_t number = arg(input, 0), out = arg(input, 1);
  int32_t precision = (int32_t)arg(input, 2);
  uint32_t record = digits(input, number);
  BB_U32(0x10020c08u) = record;
  int32_t before = position(record) - 1;
  BB_U32(0x1001fe3cu) = (uint32_t)before;
  uint32_t start = out + negative(record);
  round_digits(input, start, precision, record);
  int32_t after = position(record) - 1;
  BB_U8(0x1001fe40u) = after > before;
  BB_U32(0x1001fe3cu) = (uint32_t)after;
  uint32_t args[16] = {number, out, (uint32_t)precision, arg(input, 3)};
  if (after >= -4 && precision > after) {
    if (after > before) {
      size_t length = strlen(text(start));
      BB_U8(start + (uint32_t)length - 1) = 0;
    }
    call(input, 0x100147a0u, args);
  } else
    call(input, 0x10014770u, args);
}
/* 0x100147d0..0x10014835: e/E/f/general callback selection. */
void bb_qmix_format_number_c(uint32_t *input) {
  uint32_t format = arg(input, 2);
  uint32_t args[16] = {arg(input, 0), arg(input, 1), arg(input, 3), arg(input, 4)};
  call(input, format == 'e' || format == 'E' ? 0x10014480u :
              format == 'f' ? 0x100145c0u : 0x100146c0u, args);
}
/* 0x10016740..0x100167fc: encode a double into the CRT's ten-byte extended
 * form, including subnormal normalization and its unsigned-zero convention. */
void bb_qmix_format_double_extended_c(uint32_t *input) {
  uint32_t out = arg(input, 0), number = arg(input, 1);
  uint64_t raw;
  memcpy(&raw, text(number), sizeof(raw));
  uint64_t mantissa = (raw & UINT64_C(0x000fffffffffffff)) << 11;
  unsigned exponent = (unsigned)(raw >> 52) & 0x7ffu;
  uint16_t encoded;
  if (!exponent && !mantissa) encoded = 0;
  else {
    encoded = exponent == 0x7ffu ? 0x7fffu :
              exponent ? (uint16_t)(exponent + 0x3c00u) : 0x3c01u;
    if (exponent) mantissa |= UINT64_C(0x8000000000000000);
    while (!(mantissa & UINT64_C(0x8000000000000000))) {
      --encoded;
      mantissa <<= 1;
    }
    encoded |= (uint16_t)((raw >> 48) & 0x8000u);
  }
  memcpy(text(out), &mantissa, sizeof(mantissa));
  BB_U16(out + 8) = encoded;
}

#define ARG(n) uint32_t a##n __attribute__((unused))
#define ENTRY(name, signature)                                                 \
  uint32_t __attribute__((naked)) name##_entry signature {                     \
    __asm__ volatile(                                                          \
        "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(         \
            name##_c) ";leal 4(%esp),%esp;popal;popfl;leal 2048(%esp),%esp;ret"); \
  }
ENTRY(bb_qmix_format_decimal_insert, (ARG(0)))
ENTRY(bb_qmix_format_trim, (ARG(0)))
ENTRY(bb_qmix_format_nonnegative, (ARG(0)))
ENTRY(bb_qmix_format_parse, (ARG(0), ARG(1), ARG(2)))
ENTRY(bb_qmix_format_scientific, (ARG(0), ARG(1), ARG(2), ARG(3)))
ENTRY(bb_qmix_format_fixed, (ARG(0), ARG(1), ARG(2)))
ENTRY(bb_qmix_format_general, (ARG(0), ARG(1), ARG(2), ARG(3)))
ENTRY(bb_qmix_format_scientific_cached, (ARG(0), ARG(1), ARG(2), ARG(3)))
ENTRY(bb_qmix_format_fixed_cached, (ARG(0), ARG(1), ARG(2)))
ENTRY(bb_qmix_format_number, (ARG(0), ARG(1), ARG(2), ARG(3), ARG(4)))
ENTRY(bb_qmix_format_shift, (ARG(0), ARG(1)))
ENTRY(bb_qmix_format_digits_round, (ARG(0), ARG(1), ARG(2)))
ENTRY(bb_qmix_format_double_extended, (ARG(0), ARG(1)))
#undef ENTRY
#undef ARG
